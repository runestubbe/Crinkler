#!/usr/bin/env python3
"""Cross-build the Crinkler Visual Studio solution with MinGW-w64.

The .sln and .vcxproj files stay the single source of truth. This script reads
them at build time and translates what it finds - source lists, preprocessor
definitions, compiler/linker settings, custom build steps, resource scripts,
project references - into MinGW-w64 command lines. Adding a source file or
changing a setting in Visual Studio therefore needs no change here.

Usage:
    python3 crossbuild/build.py                      # Release|x64 Crinkler
    python3 crossbuild/build.py --platform Win32     # Release|Win32 Crinkler
    python3 crossbuild/build.py --config Debug
    python3 crossbuild/build.py --project all
    python3 crossbuild/build.py --clean

See crossbuild/README.md for the settings translation table and the known
limits of the translation.
"""

import argparse
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor

MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"
NS = {"ms": MSBUILD_NS}

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CROSSBUILD_DIR = os.path.join(REPO_ROOT, "crossbuild")
COMPAT_DIR = os.path.join(CROSSBUILD_DIR, "compat")

# Toolchain prefixes per Visual Studio platform name.
PLATFORMS = {
    "Win32": "i686-w64-mingw32",
    "x64": "x86_64-w64-mingw32",
}


class BuildError(Exception):
    pass


# --------------------------------------------------------------------------
# Minimal MSBuild evaluation
# --------------------------------------------------------------------------

def tag(elem):
    """Local name of an element, without the MSBuild namespace."""
    return elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag


def text_of(elem):
    return (elem.text or "").strip()


class Evaluator:
    """Expands $(Property) references and evaluates the Condition subset that
    Visual Studio's own project generator emits."""

    def __init__(self, properties):
        self.properties = properties

    def expand(self, value):
        if not value or "$(" not in value:
            return value or ""
        # Repeat so that properties defined in terms of other properties resolve.
        for _ in range(10):
            new_value = re.sub(
                r"\$\(([A-Za-z_][A-Za-z0-9_]*)\)",
                lambda m: self.properties.get(m.group(1), ""),
                value,
            )
            if new_value == value:
                break
            value = new_value
        return value

    def condition_holds(self, condition):
        if not condition:
            return True
        expanded = self.expand(condition).strip()

        # exists('...') - only ever used for optional per-user property sheets,
        # which have no meaning outside Visual Studio.
        if expanded.lower().startswith("exists("):
            return False

        match = re.fullmatch(r"\s*'(.*?)'\s*(==|!=)\s*'(.*?)'\s*", expanded, re.S)
        if match:
            left, op, right = match.groups()
            equal = left.strip().lower() == right.strip().lower()
            return equal if op == "==" else not equal

        # Anything more elaborate than that is not something this translation
        # understands; skipping is the safe direction (it can only leave
        # settings out, never invent wrong ones), but say so.
        sys.stderr.write("warning: ignoring unsupported condition: %s\n" % expanded)
        return False


class Item:
    """One <ClCompile>/<CustomBuild>/... entry plus its metadata."""

    def __init__(self, include, project_dir, metadata):
        self.include = include
        self.metadata = metadata
        self.path = os.path.normpath(os.path.join(project_dir, include.replace("\\", "/")))

    def well_known_metadata(self):
        directory = os.path.dirname(self.path)
        root = os.path.splitdrive(self.path)[0] + os.sep
        return {
            "Identity": self.include,
            "FullPath": self.path,
            "RootDir": root,
            "Filename": os.path.splitext(os.path.basename(self.path))[0],
            "Extension": os.path.splitext(self.path)[1],
            "Directory": directory[len(root):] + os.sep,
            "RelativeDir": os.path.dirname(self.include.replace("\\", "/")),
        }


class Project:
    """A .vcxproj evaluated for one Configuration|Platform pair."""

    def __init__(self, path, configuration, platform, solution_dir, out_dir, int_dir):
        self.path = os.path.abspath(path)
        self.dir = os.path.dirname(self.path)
        self.name = os.path.splitext(os.path.basename(self.path))[0]
        self.configuration = configuration
        self.platform = platform

        self.properties = {
            "Configuration": configuration,
            "Platform": platform,
            "ProjectName": self.name,
            "RootNamespace": self.name,
            "ProjectDir": self.dir + os.sep,
            "ProjectPath": self.path,
            "ProjectFileName": os.path.basename(self.path),
            "ProjectExt": ".vcxproj",
            "SolutionDir": solution_dir + os.sep,
            "MSBuildProjectDirectory": self.dir,
            "MSBuildProjectName": self.name,
        }
        self.evaluator = Evaluator(self.properties)

        self.item_definitions = {}   # tool name -> {metadata: value}
        self.items = {}              # item type -> [Item]

        self._parse()

        # The output locations are the one thing deliberately not taken from the
        # project: MSVC and MinGW would otherwise write different binaries to
        # the same paths and clobber each other.
        self.out_dir = out_dir
        self.int_dir = int_dir
        self.properties["OutDir"] = out_dir + os.sep
        self.properties["IntDir"] = int_dir + os.sep
        self.properties["IntermediateOutputPath"] = int_dir + os.sep
        self.properties.setdefault("TargetName", self.name)

    def _parse(self):
        root = ET.parse(self.path).getroot()
        for elem in root:
            name = tag(elem)
            if name == "PropertyGroup":
                self._parse_property_group(elem)
            elif name == "ItemDefinitionGroup":
                self._parse_item_definition_group(elem)
            elif name == "ItemGroup":
                self._parse_item_group(elem)

    def _parse_property_group(self, group):
        if not self.evaluator.condition_holds(group.get("Condition")):
            return
        for prop in group:
            if not self.evaluator.condition_holds(prop.get("Condition")):
                continue
            self.properties[tag(prop)] = self.evaluator.expand(text_of(prop))

    def _parse_item_definition_group(self, group):
        if not self.evaluator.condition_holds(group.get("Condition")):
            return
        for tool in group:
            settings = self.item_definitions.setdefault(tag(tool), {})
            for setting in tool:
                if not self.evaluator.condition_holds(setting.get("Condition")):
                    continue
                # Kept unexpanded: settings may refer to properties such as
                # $(OutDir) that are only final once the whole file is read.
                settings[tag(setting)] = text_of(setting)

    def _parse_item_group(self, group):
        if not self.evaluator.condition_holds(group.get("Condition")):
            return
        for elem in group:
            include = elem.get("Include")
            if include is None:
                continue
            metadata = {}
            for meta in elem:
                if not self.evaluator.condition_holds(meta.get("Condition")):
                    continue
                metadata[tag(meta)] = text_of(meta)
            self.items.setdefault(tag(elem), []).append(
                Item(include, self.dir, metadata))

    def setting(self, tool, name, default="", item=None, raw=False):
        """A tool setting, with any per-item override taking precedence."""
        if item is not None and name in item.metadata:
            value = item.metadata[name]
        else:
            value = self.item_definitions.get(tool, {}).get(name, default)
        return value if raw else self.evaluator.expand(value)

    def configuration_type(self):
        return self.properties.get("ConfigurationType", "Application")

    def target_path(self):
        output_file = self.setting("Link", "OutputFile")
        if output_file and self.configuration_type() == "Application":
            return os.path.normpath(
                os.path.join(self.dir, self.evaluator.expand(output_file).replace("\\", "/")))
        target = self.properties.get("TargetName", self.name)
        extension = ".exe" if self.configuration_type() == "Application" else ".a"
        return os.path.join(self.out_dir, target + extension)


def parse_solution(sln_path):
    """Returns (projects, by_guid, config_map).

    projects   name -> {name, path, guid, depends_on: [guid]}
    by_guid    guid -> the same dicts
    config_map (guid, "Config|Platform") -> "Config|Platform" for the project
    """
    projects = {}
    by_guid = {}
    config_map = {}

    with open(sln_path, "r", encoding="utf-8-sig") as f:
        lines = f.readlines()

    current = None
    in_dependencies = False
    for line in lines:
        stripped = line.strip()

        match = re.match(
            r'Project\("\{[^}]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+)",\s*"\{([^}]+)\}"',
            stripped)
        if match:
            name, rel_path, guid = match.groups()
            current = {
                "name": name,
                "path": os.path.normpath(
                    os.path.join(os.path.dirname(sln_path), rel_path.replace("\\", "/"))),
                "guid": guid.upper(),
                "depends_on": [],
            }
            projects[name] = current
            by_guid[current["guid"]] = current
            continue

        if stripped.startswith("ProjectSection(ProjectDependencies)"):
            in_dependencies = True
            continue
        if stripped.startswith("EndProjectSection"):
            in_dependencies = False
            continue
        if in_dependencies and current is not None:
            match = re.match(r"\{([^}]+)\}\s*=\s*\{([^}]+)\}", stripped)
            if match:
                current["depends_on"].append(match.group(1).upper())
            continue

        if stripped.startswith("EndProject"):
            current = None
            continue

        match = re.match(
            r"\{([^}]+)\}\.([^.]+)\.ActiveCfg\s*=\s*(.+)$", stripped)
        if match:
            guid, solution_config, project_config = match.groups()
            config_map[(guid.upper(), solution_config)] = project_config.strip()

    return projects, by_guid, config_map


# --------------------------------------------------------------------------
# MSVC settings -> MinGW flags
# --------------------------------------------------------------------------

OPTIMIZATION_FLAGS = {
    "Disabled": ["-O0"],
    "MinSpace": ["-Os"],
    "MaxSpeed": ["-O2"],
    "Full": ["-O3"],
}

LANGUAGE_STANDARD_FLAGS = {
    "stdcpp14": "-std=c++14",
    "stdcpp17": "-std=c++17",
    "stdcpp20": "-std=c++20",
    "stdcpp23": "-std=c++23",
    "stdcpplatest": "-std=c++23",
}

INSTRUCTION_SET_FLAGS = {
    "SSE": ["-msse"],
    "SSE2": ["-msse2"],
    "AVX": ["-mavx"],
    "AVX2": ["-mavx2"],
    "AVX512": ["-mavx512f"],
    "NoExtensions": ["-mno-sse"],
}

# Things the MSVC toolchain implies but GCC has to be told explicitly, plus the
# compatibility shims in crossbuild/compat. This is the only place where the
# cross build adds anything the project files do not ask for.
TOOLCHAIN_COMPILE_FLAGS = [
    # MSVC lets any intrinsic be used regardless of /arch; GCC gates each one
    # behind its ISA flag. Crinkler uses _mm_crc32_* (SSE4.2) unconditionally.
    "-msse4.2",
]
TOOLCHAIN_LINK_FLAGS = [
    # Produce a self-contained .exe, like the statically linked MSVC CRT does.
    "-static",
    "-static-libgcc",
    "-static-libstdc++",
]
TOOLCHAIN_INCLUDE_DIRS = [COMPAT_DIR]
# Linked into every Application: MSVC compiler-support routines that the
# prebuilt distorm .lib files reference and MinGW does not provide.
TOOLCHAIN_SUPPORT_SOURCES = [os.path.join(COMPAT_DIR, "msvc_stubs.c")]


def split_list(value):
    """Splits an MSBuild semicolon list, dropping %(Inherited) placeholders."""
    if not value:
        return []
    return [part.strip() for part in value.split(";")
            if part.strip() and not part.strip().startswith("%(")]


def is_true(value):
    return str(value).strip().lower() == "true"


def is_false(value):
    return str(value).strip().lower() == "false"


def additional_options(value):
    """MSVC extra options that have a GCC meaning. Everything /slash-style is
    MSVC-only (e.g. /MP, which this script replaces with its own job pool)."""
    if not value:
        return []
    return [opt for opt in shlex.split(value.replace("%(AdditionalOptions)", ""))
            if opt.startswith("-")]


def compile_flags(project, item):
    """Translates ClCompile settings into GCC flags."""
    get = lambda name, default="": project.setting("ClCompile", name, default, item)
    flags = []

    flags += OPTIMIZATION_FLAGS.get(get("Optimization"), [])

    standard = get("LanguageStandard").lower()
    if standard in LANGUAGE_STANDARD_FLAGS:
        flags.append(LANGUAGE_STANDARD_FLAGS[standard])

    flags += INSTRUCTION_SET_FLAGS.get(get("EnableEnhancedInstructionSet"), [])

    if is_true(get("OmitFramePointers")):
        flags.append("-fomit-frame-pointer")
    elif is_false(get("OmitFramePointers")):
        flags.append("-fno-omit-frame-pointer")

    if is_true(get("WholeProgramOptimization")):
        flags.append("-flto")

    if is_false(get("BufferSecurityCheck")):
        flags.append("-fno-stack-protector")
    elif is_true(get("BufferSecurityCheck")):
        flags.append("-fstack-protector")

    if is_false(get("RuntimeTypeInfo")):
        flags.append("-fno-rtti")

    if is_true(get("OpenMPSupport")):
        flags.append("-fopenmp")

    floating_point = get("FloatingPointModel")
    if floating_point == "Fast":
        flags += ["-ffp-contract=fast", "-fno-math-errno"]
    elif floating_point == "Strict":
        flags += ["-ffp-contract=off", "-frounding-math"]
    elif floating_point == "Precise":
        flags.append("-ffp-contract=off")

    warning_level = get("WarningLevel")
    if warning_level in ("TurnOffAllWarnings", "Level0"):
        flags.append("-w")
    elif warning_level in ("Level4", "EnableAllWarnings"):
        flags.append("-Wall")
    if is_true(get("TreatWarningAsError")):
        flags.append("-Werror")

    if get("DebugInformationFormat") not in ("", "None"):
        flags.append("-g")

    if get("PrecompiledHeader") == "Use":
        raise BuildError(
            "%s uses precompiled headers, which this cross build does not "
            "translate" % project.name)

    for define in split_list(get("PreprocessorDefinitions")):
        flags.append("-D" + define)
    for include in split_list(get("AdditionalIncludeDirectories")):
        flags.append("-I" + os.path.normpath(
            os.path.join(project.dir, include.replace("\\", "/"))))

    flags += additional_options(get("AdditionalOptions"))
    return flags


def link_flags(project, object_files, library_paths):
    """Translates Link settings into GCC driver arguments."""
    get = lambda name, default="": project.setting("Link", name, default)
    flags = list(object_files)

    for directory in split_list(get("AdditionalLibraryDirectories")):
        flags.append("-L" + os.path.normpath(
            os.path.join(project.dir, directory.replace("\\", "/"))))

    flags += library_paths

    for dependency in split_list(get("AdditionalDependencies")):
        dependency = dependency.replace("\\", "/")
        if "/" in dependency:
            # A path to a specific library file - hand it to the linker as-is.
            flags.append(os.path.normpath(os.path.join(project.dir, dependency)))
        else:
            flags.append("-l" + re.sub(r"\.lib$", "", dependency, flags=re.I).lower())

    subsystem = get("SubSystem")
    if subsystem == "Console":
        flags.append("-mconsole")
    elif subsystem == "Windows":
        flags.append("-mwindows")

    entry_point = get("EntryPointSymbol")
    if entry_point:
        flags.append("-Wl,-e," + entry_point)

    if is_true(get("IgnoreAllDefaultLibraries")):
        flags.append("-nostdlib")

    if get("GenerateDebugInformation") not in ("", "false", "No", "None"):
        flags.append("-g")

    if is_true(get("GenerateMapFile")):
        flags.append("-Wl,-Map," + os.path.join(
            project.out_dir, project.properties.get("TargetName", project.name) + ".map"))

    flags += additional_options(get("AdditionalOptions"))
    flags += TOOLCHAIN_LINK_FLAGS
    return flags


def resource_flags(project, item):
    get = lambda name, default="": project.setting("ResourceCompile", name, default, item)
    flags = []
    for define in split_list(get("PreprocessorDefinitions")):
        flags.append("-D" + define)
    for include in split_list(get("AdditionalIncludeDirectories")):
        flags.append("-I" + os.path.normpath(
            os.path.join(project.dir, include.replace("\\", "/"))))
    return flags


# --------------------------------------------------------------------------
# Custom build steps
# --------------------------------------------------------------------------

def expand_item_metadata(text, item):
    """Replaces %(Name) references with the item's well-known metadata.

    Only well-known metadata is substituted. A reference to the metadata being
    defined - %(Outputs) inside Outputs, say - is MSBuild's "inherit whatever
    was there before" marker, and callers drop those list entries instead.
    """
    metadata = item.well_known_metadata()
    return re.sub(r"%\(([A-Za-z_][A-Za-z0-9_]*)\)",
                  lambda m: metadata.get(m.group(1), m.group(0)), text)


def to_posix_command(command):
    """Turns a Windows custom-build command line into one /bin/sh can run.

    Custom build steps in this solution are plain tool invocations whose only
    backslashes are path separators, so converting them wholesale is safe. A
    step that used backslashes for anything else (escapes, batch built-ins like
    `if exist` or `copy`) would need translating by hand.
    """
    command = command.replace("\\", "/")
    return re.sub(r"(?<!:)/{2,}", "/", command)


def custom_build_setting(project, item, name):
    """A CustomBuild metadata value with both %(item) and $(property)
    references resolved, in that order."""
    value = project.setting("CustomBuild", name, "", item, raw=True)
    return project.evaluator.expand(expand_item_metadata(value, item))


def custom_build_commands(project, item):
    command = custom_build_setting(project, item, "Command")
    if not command.strip():
        return []
    return [to_posix_command(line) for line in command.splitlines() if line.strip()]


def custom_build_outputs(project, item):
    outputs = custom_build_setting(project, item, "Outputs")
    return [os.path.normpath(to_posix_command(path))
            for path in split_list(outputs)]


def custom_build_inputs(project, item):
    inputs = [item.path]
    extra = custom_build_setting(project, item, "AdditionalInputs")
    for path in split_list(extra):
        inputs.append(os.path.normpath(os.path.join(project.dir, path.replace("\\", "/"))))
    return inputs


# --------------------------------------------------------------------------
# Running the build
# --------------------------------------------------------------------------

class Builder:
    def __init__(self, args):
        self.args = args
        self.verbose = args.verbose
        self.jobs = args.jobs
        self.prefix = PLATFORMS[args.platform]
        self.cc = self.prefix + "-gcc"
        self.cxx = self.prefix + "-g++"
        self.ar = self.prefix + "-ar"
        self.windres = self.prefix + "-windres"
        self.objcopy = self.prefix + "-objcopy"

    def tool_check(self):
        missing = [tool for tool in (self.cc, self.cxx, self.ar, self.windres,
                                     self.objcopy, "nasm")
                   if shutil.which(tool) is None]
        if missing:
            raise BuildError(
                "missing required tool(s): %s\n"
                "Install with: brew install mingw-w64 nasm" % ", ".join(missing))

    def run(self, command, cwd=None, description=None):
        if self.verbose:
            printable = command if isinstance(command, str) else " ".join(
                shlex.quote(part) for part in command)
            print("  " + printable)
        elif description:
            print("  " + description)
        result = subprocess.run(
            command, cwd=cwd, shell=isinstance(command, str),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = result.stdout.decode("utf-8", "replace")
        # MinGW's ld warns about the .drectve sections in MSVC-produced .lib
        # files; the directives it cannot parse are MSVC-only linker hints.
        output = "\n".join(line for line in output.splitlines()
                           if "corrupt .drectve" not in line)
        if output.strip():
            sys.stdout.write(output.rstrip() + "\n")
        if result.returncode != 0:
            raise BuildError("command failed: %s" % (
                command if isinstance(command, str) else " ".join(command)))

    # -- up-to-date checks --------------------------------------------------

    @staticmethod
    def newest(paths):
        newest = 0.0
        for path in paths:
            try:
                newest = max(newest, os.path.getmtime(path))
            except OSError:
                return None  # A missing input forces a rebuild.
        return newest

    def is_current(self, outputs, inputs, stamp_path, command_key):
        """True when every output is newer than every input and the command
        line that produced them has not changed."""
        if self.args.force:
            return False
        digest = hashlib.sha256(command_key.encode()).hexdigest()
        try:
            with open(stamp_path) as f:
                if f.read().strip() != digest:
                    return False
        except OSError:
            return False

        oldest_output = None
        for output in outputs:
            try:
                mtime = os.path.getmtime(output)
            except OSError:
                return False
            oldest_output = mtime if oldest_output is None else min(oldest_output, mtime)

        newest_input = self.newest(inputs)
        if newest_input is None:
            return False
        return oldest_output >= newest_input

    @staticmethod
    def write_stamp(stamp_path, command_key):
        os.makedirs(os.path.dirname(stamp_path), exist_ok=True)
        with open(stamp_path, "w") as f:
            f.write(hashlib.sha256(command_key.encode()).hexdigest())

    @staticmethod
    def read_depfile(path):
        """Header dependencies recorded by gcc -MMD."""
        try:
            with open(path) as f:
                content = f.read()
        except OSError:
            return []
        content = content.replace("\\\n", " ")
        deps = []
        for line in content.splitlines():
            if ":" not in line:
                continue
            deps += shlex.split(line.split(":", 1)[1])
        return deps

    # -- build steps --------------------------------------------------------

    def build_project(self, project, library_paths):
        print("== %s (%s|%s)" % (project.name, project.configuration, project.platform))
        os.makedirs(project.out_dir, exist_ok=True)
        os.makedirs(project.int_dir, exist_ok=True)

        objects = []
        objects += self.run_custom_builds(project)
        objects += self.compile_sources(project)
        objects += self.compile_resources(project)

        if project.configuration_type() == "Application":
            objects += self.compile_support_sources(project)
            return self.link_application(project, objects, library_paths)
        return self.archive_library(project, objects)

    def run_custom_builds(self, project):
        """Runs <CustomBuild> steps; their object-file outputs join the link."""
        produced = []
        for item in project.items.get("CustomBuild", []):
            commands = custom_build_commands(project, item)
            if not commands:
                continue
            outputs = custom_build_outputs(project, item)
            inputs = custom_build_inputs(project, item)
            stamp = os.path.join(project.int_dir,
                                 os.path.basename(item.path) + ".custom.stamp")
            key = "\n".join(commands)

            if not self.is_current(outputs, inputs, stamp, key):
                message = custom_build_setting(project, item, "Message")
                print("  %s" % (message or os.path.basename(item.path)))
                for command in commands:
                    self.run(command, cwd=project.dir)
                self.write_stamp(stamp, key)

            produced += [path for path in outputs
                         if os.path.splitext(path)[1].lower() in (".obj", ".o")]
        return produced

    def compile_sources(self, project):
        items = project.items.get("ClCompile", [])
        jobs = []
        for item in items:
            obj = os.path.join(
                project.int_dir,
                re.sub(r"[\\/]", "_", os.path.splitext(item.include)[0]) + ".o")
            flags = compile_flags(project, item)
            is_c = os.path.splitext(item.path)[1].lower() == ".c"
            compiler = self.cc if is_c else self.cxx
            command = ([compiler] + flags + TOOLCHAIN_COMPILE_FLAGS
                       + ["-I" + d for d in TOOLCHAIN_INCLUDE_DIRS]
                       + ["-MMD", "-MF", obj + ".d", "-c", item.path, "-o", obj])
            jobs.append((item, obj, command))

        self.run_jobs(project, jobs)
        return [obj for _, obj, _ in jobs]

    def compile_support_sources(self, project):
        jobs = []
        for source in TOOLCHAIN_SUPPORT_SOURCES:
            obj = os.path.join(project.int_dir,
                               os.path.splitext(os.path.basename(source))[0] + ".o")
            command = [self.cc, "-O2", "-MMD", "-MF", obj + ".d", "-c", source, "-o", obj]
            jobs.append((None, obj, command))
        self.run_jobs(project, jobs)
        return [obj for _, obj, _ in jobs]

    def compile_resources(self, project):
        objects = []
        for item in project.items.get("ResourceCompile", []):
            obj = os.path.join(project.int_dir,
                               os.path.splitext(os.path.basename(item.path))[0] + ".res.o")
            command = ([self.windres]
                       + resource_flags(project, item)
                       + ["-I" + project.dir]
                       + ["-I" + d for d in TOOLCHAIN_INCLUDE_DIRS]
                       + ["-O", "coff", item.path, obj])
            key = " ".join(command)
            stamp = obj + ".stamp"
            if not self.is_current([obj], [item.path], stamp, key):
                print("  %s" % os.path.basename(item.path))
                self.run(command, cwd=project.dir)
                self.write_stamp(stamp, key)
            objects.append(obj)
        return objects

    def run_jobs(self, project, jobs):
        """Compiles in parallel, honouring recorded header dependencies."""
        pending = []
        for item, obj, command in jobs:
            source = command[command.index("-c") + 1]
            deps = [source] + self.read_depfile(obj + ".d")
            key = " ".join(command)
            if self.is_current([obj], deps, obj + ".stamp", key):
                continue
            pending.append((source, obj, command, key))

        if not pending:
            return

        errors = []

        def compile_one(job):
            source, obj, command, key = job
            try:
                print("  %s" % os.path.relpath(source, REPO_ROOT))
                self.run(command, cwd=project.dir)
                self.write_stamp(obj + ".stamp", key)
            except BuildError as error:
                errors.append(error)

        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            list(pool.map(compile_one, pending))

        if errors:
            raise BuildError("%d compilation(s) failed" % len(errors))

    def link_application(self, project, objects, library_paths):
        target = project.target_path()
        os.makedirs(os.path.dirname(target), exist_ok=True)
        command = [self.cxx, "-o", target] + link_flags(project, objects, library_paths)
        key = " ".join(command)
        stamp = os.path.join(project.int_dir, os.path.basename(target) + ".stamp")
        if self.is_current([target], objects + library_paths, stamp, key):
            print("  %s is up to date" % os.path.relpath(target, REPO_ROOT))
            return target
        print("  linking %s" % os.path.relpath(target, REPO_ROOT))
        self.run(command, cwd=project.dir)
        self.split_debug_info(project, target)
        self.write_stamp(stamp, key)
        return target

    def split_debug_info(self, project, target):
        """Moves debug info into a companion file, the way MSVC's /DEBUG keeps
        it in a .pdb rather than in the executable itself."""
        generate = project.setting("Link", "GenerateDebugInformation")
        if generate in ("", "false", "No", "None"):
            return
        debug_file = os.path.splitext(target)[0] + ".debug"
        self.run([self.objcopy, "--only-keep-debug", target, debug_file])
        self.run([self.objcopy, "--strip-debug",
                  "--add-gnu-debuglink=" + debug_file, target])

    def archive_library(self, project, objects):
        target = project.target_path()
        os.makedirs(os.path.dirname(target), exist_ok=True)
        command = [self.ar, "rcs", target] + objects
        key = " ".join(command)
        stamp = os.path.join(project.int_dir, os.path.basename(target) + ".stamp")
        if self.is_current([target], objects, stamp, key):
            print("  %s is up to date" % os.path.relpath(target, REPO_ROOT))
            return target
        print("  archiving %s" % os.path.relpath(target, REPO_ROOT))
        if os.path.exists(target):
            os.remove(target)
        self.run(command, cwd=project.dir)
        self.write_stamp(stamp, key)
        return target


# --------------------------------------------------------------------------

def output_dirs(platform, configuration, project_name):
    suffix = os.path.join("mingw-" + platform, configuration, project_name)
    return (os.path.join(REPO_ROOT, "build", suffix),
            os.path.join(REPO_ROOT, "artifacts", suffix))


def load_project(project_info, solution_config, config_map, sln_dir):
    """Evaluates a project for the configuration the solution maps it to."""
    project_config = config_map.get(
        (project_info["guid"], solution_config), solution_config)
    configuration, platform = project_config.split("|")
    if platform not in PLATFORMS:
        raise BuildError("unsupported platform %r for project %s"
                         % (platform, project_info["name"]))
    out_dir, int_dir = output_dirs(platform, configuration, project_info["name"])
    return Project(project_info["path"], configuration, platform,
                   sln_dir, out_dir, int_dir)


def collect_build_order(project_info, by_guid, solution_config,
                        config_map, sln_dir, seen=None):
    """Returns [(project_info, Project)] with dependencies before dependents."""
    if seen is None:
        seen = set()
    if project_info["guid"] in seen:
        return []
    seen.add(project_info["guid"])

    project = load_project(project_info, solution_config, config_map, sln_dir)

    dependencies = list(project_info["depends_on"])
    for reference in project.items.get("ProjectReference", []):
        guid = reference.metadata.get("Project", "").strip("{}").upper()
        if guid:
            dependencies.append(guid)

    order = []
    for guid in dependencies:
        dependency = by_guid.get(guid)
        if dependency is not None:
            order += collect_build_order(dependency, by_guid, solution_config,
                                         config_map, sln_dir, seen)
    order.append((project_info, project))
    return order


def clean(platform, configuration):
    for root in ("build", "artifacts"):
        path = os.path.join(REPO_ROOT, root, "mingw-" + platform, configuration)
        if os.path.isdir(path):
            print("removing %s" % os.path.relpath(path, REPO_ROOT))
            shutil.rmtree(path)


def main():
    parser = argparse.ArgumentParser(
        description="Cross-build the Crinkler MSVC solution with MinGW-w64.")
    parser.add_argument("--sln", default=os.path.join(REPO_ROOT, "Crinkler.sln"),
                        help="solution file to build (default: Crinkler.sln)")
    parser.add_argument("--platform", default="x64", choices=sorted(PLATFORMS),
                        help="solution platform (default: x64)")
    parser.add_argument("--config", default="Release",
                        help="solution configuration (default: Release)")
    parser.add_argument("--project", default="Crinkler",
                        help='project to build, or "all" (default: Crinkler)')
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4,
                        help="parallel compile jobs")
    parser.add_argument("-B", "--force", action="store_true",
                        help="rebuild everything, ignoring up-to-date checks")
    parser.add_argument("--clean", action="store_true",
                        help="remove this configuration's outputs and exit")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print every command")
    args = parser.parse_args()

    if args.clean:
        clean(args.platform, args.config)
        return 0

    builder = Builder(args)
    builder.tool_check()

    sln_path = os.path.abspath(args.sln)
    sln_dir = os.path.dirname(sln_path)
    projects, by_guid, config_map = parse_solution(sln_path)

    solution_config = "%s|%s" % (args.config, args.platform)
    if args.project.lower() == "all":
        roots = list(projects.values())
    elif args.project in projects:
        roots = [projects[args.project]]
    else:
        raise BuildError("no project %r in %s (have: %s)"
                         % (args.project, os.path.basename(sln_path),
                            ", ".join(sorted(projects))))

    order = []
    seen = set()
    for root in roots:
        order += collect_build_order(root, by_guid, solution_config,
                                     config_map, sln_dir, seen)

    libraries = []
    targets = []
    for _, project in order:
        target = builder.build_project(project, list(libraries))
        if project.configuration_type() == "StaticLibrary":
            libraries.append(target)
        else:
            targets.append(target)

    print()
    for target in targets:
        print("built %s" % os.path.relpath(target, REPO_ROOT))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BuildError as error:
        sys.stdout.flush()
        sys.stderr.write("error: %s\n" % error)
        sys.exit(1)
