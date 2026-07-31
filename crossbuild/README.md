# Cross-building Crinkler on macOS/Linux

`crossbuild/build.py` builds the Windows Crinkler executable from a Unix host
using MinGW-w64. It reads `Crinkler.sln` and the `.vcxproj` files directly, so
**the Visual Studio projects remain the source of truth** - adding a source
file, changing a preprocessor definition or editing a custom build step in
Visual Studio needs no corresponding change here.

Both 32-bit and 64-bit Crinkler build. (The two produce identical output; the
platform only decides what kind of executable Crinkler itself is.)

## Prerequisites

```bash
brew install mingw-w64 nasm
```

`wine` is needed only for running the result:

```bash
brew install --cask wine-stable
```

## Building

```bash
python3 crossbuild/build.py                                  # Release|x64 Crinkler
python3 crossbuild/build.py --platform Win32                 # 32-bit
python3 crossbuild/build.py --config Debug
python3 crossbuild/build.py --project all                    # every project in the solution
python3 crossbuild/build.py --clean
python3 crossbuild/build.py -v                               # show every command
```

Output goes to `build/mingw-<platform>/<config>/<project>/`, with intermediates
in `artifacts/mingw-<platform>/<config>/<project>/`. These are deliberately kept
apart from the MSVC output directories so the two builds cannot clobber each
other. Builds are incremental: object files track header dependencies via
`gcc -MMD`, and every step re-runs if its command line changed.

## Testing with Wine

Crinkler itself runs fine under Wine, so a built binary can be checked directly:

```bash
wine build/mingw-x64/Release/Crinkler/Crinkler.exe /OUT:out.exe /SUBSYSTEM:WINDOWS \
    /LIBPATH:test/libs test/als/main.obj test/als/midi.obj test/als/miniglsl.obj \
    /RANGE:opengl32 kernel32.lib opengl32.lib user32.lib gdi32.lib winmm.lib msvcrt_old.lib
```

`CRINKLER_THREADS=<n>` caps the worker pool, which is useful when measuring.

## How settings are translated

`build.py` implements enough of MSBuild to evaluate the project files: property
groups with the `'$(Configuration)|$(Platform)'=='...'` conditions Visual Studio
generates, `ItemDefinitionGroup` tool settings, per-item metadata overrides,
`ProjectReference` dependencies, and the solution's per-project configuration
mapping (which is how `ExportScraper` still builds as Win32 inside an x64
solution build).

| MSVC setting | MinGW translation |
| --- | --- |
| `Optimization` | `-O0` / `-Os` / `-O2` / `-O3` |
| `LanguageStandard` | `-std=c++14/17/20/23` |
| `PreprocessorDefinitions` | `-D` |
| `AdditionalIncludeDirectories` | `-I` |
| `EnableEnhancedInstructionSet` | `-msse` / `-msse2` / `-mavx` / ... |
| `OmitFramePointers` | `-f[no-]omit-frame-pointer` |
| `BufferSecurityCheck` | `-f[no-]stack-protector` |
| `RuntimeTypeInfo` | `-fno-rtti` |
| `WholeProgramOptimization` | `-flto` |
| `OpenMPSupport` | `-fopenmp` |
| `FloatingPointModel` | `-ffp-contract=fast -fno-math-errno` / `-ffp-contract=off` |
| `WarningLevel`, `TreatWarningAsError` | `-w` / `-Wall` / `-Werror` |
| `DebugInformationFormat` | `-g`, split into a `.debug` file after linking |
| `AdditionalDependencies` | `-l<name>`, or the file path when one is given |
| `SubSystem` | `-mconsole` / `-mwindows` |
| `EntryPointSymbol` | `-Wl,-e,<symbol>` |
| `GenerateMapFile` | `-Wl,-Map,...` |
| `IgnoreAllDefaultLibraries` | `-nostdlib` |
| `ResourceCompile` | `windres -O coff` |
| `CustomBuild` | the command line, with `$(...)`/`%(...)` expanded and backslashes turned into slashes |

Settings with no GCC counterpart are ignored: `RuntimeLibrary`,
`MinimalRebuild`, `BasicRuntimeChecks`, `StringPooling`, `IntrinsicFunctions`,
`EnableCOMDATFolding`, `ImageHasSafeExceptionHandlers`, `DisableSpecificWarnings`
(MSVC warning numbers), and `/`-style `AdditionalOptions` such as `/MP` (the
script runs its own parallel job pool). `OptimizeReferences` is deliberately not
mapped to `--gc-sections`, which could discard the `incbin`-ed module blobs.

### Deliberate deviations

Two places where the build does not follow the project files literally:

- **Output directories** are `build/mingw-*` / `artifacts/mingw-*` rather than
  the project's `OutDir`/`IntDir`, so MSVC and MinGW builds coexist.
- **Debug info is split** into a companion `.debug` file and linked back with
  `--add-gnu-debuglink`, mirroring how MSVC keeps it in a `.pdb` instead of in
  the executable.

## Compatibility shims (`crossbuild/compat/`)

These are only on the include/link path for the cross build; the MSVC build is
untouched by them.

- **`ppl.h`** - stand-in for the Microsoft Parallel Patterns Library, providing
  the `parallel_for`, `critical_section` and `combinable` that Crinkler uses, on
  top of `std::thread`. It keeps a persistent worker pool and runs small regions
  inline, both of which matter enormously: Crinkler's model optimisation
  dispatches ~170k parallel regions of 4-15 items each, and a per-region
  thread-wake handshake there costs far more than the work itself.
- **`afxres.h`** - the MFC resource header Visual Studio's editor writes into
  generated `.rc` files; forwards to `winresrc.h`, which is all the script needs.
- **`msvc_stubs.c`** - MSVC compiler-support routines referenced by the prebuilt
  `external/distorm/*.lib` files that MinGW's runtime does not provide:
  `__security_cookie` / `__security_check_cookie` / `__GSHandlerCheck` from
  `/GS`, and the `__aullshr` / `__allshr` / `__allshl` 64-bit shift helpers on
  32-bit. Linked into every Application target.

MinGW's linker warns `corrupt .drectve` for each MSVC-produced object in
`distorm.lib`; the directives it cannot parse are MSVC-only linker hints, so the
script filters those lines out.

## Limitations of the build script

- Custom build steps are assumed to be plain tool invocations whose backslashes
  are all path separators. A step using batch built-ins (`if exist`, `copy`) or
  backslash escapes would need translating by hand.
- Precompiled headers (`PrecompiledHeader = Use`) are not supported; the script
  stops with an error rather than building something subtly different.
- Conditions more elaborate than `'a'=='b'` / `'a'!='b'` are skipped with a
  warning on stderr, and `exists(...)` is treated as false (it only ever guards
  optional per-user property sheets).
