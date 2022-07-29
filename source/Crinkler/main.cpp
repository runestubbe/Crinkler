#include <windows.h>
#include <cstdio>
#include <vector>
#include <set>
#include <string>
#include <direct.h>

#include "CoffObjectLoader.h"
#include "CoffLibraryLoader.h"
#include "HunkList.h"
#include "Hunk.h"
#include "Crinkler.h"
#include "CmdLineInterFace/CmdLineInterface.h"
#include "Log.h"
#include "StringMisc.h"
#include "CallTransform.h"
#include "Fix.h"
#include "Reuse.h"
#include "MemoryFile.h"
#include "misc.h"
#include "NameMangling.h"
#include "MiniDump.h"
#include "ImportHandler.h"

using namespace std;

static bool FileExists(const char* filename) {
	FILE* file;
	fopen_s(&file, filename, "r");
	if(file != NULL) {
		fclose(file);
		return true;
	}
	return false;
}

static void FindFileInPathInner(vector<string> &res, std::string path, const char* filename)
{
	if(path[path.size() - 1] != '\\' && path[path.size() - 1] != '/')
		path += "\\";
	path += filename;

	char canonicalName[1024];
	GetFullPathName(path.c_str(), sizeof(canonicalName), canonicalName, NULL);

	if(FileExists(canonicalName)) {
		res.push_back(ToUpper(canonicalName));
	}
}

// Returns a list of found files in uppercase
static vector<string> FindFileInPath(const char* filename, const char* path_string, bool isDll) {
	vector<string> res;
	string str = path_string;

	char canonicalName[1024];
	if(GetFullPathName(filename, sizeof(canonicalName), canonicalName, NULL) && FileExists(canonicalName)) {
		res.push_back(ToUpper(canonicalName));
	}

	string delimiters = ";";
	string::size_type lastPos = str.find_first_not_of(delimiters, 0);
	string::size_type pos     = str.find_first_of(delimiters, lastPos);

	while(string::npos != pos || string::npos != lastPos)
	{
		// Found a token, add it to the vector.
		string path = ToUpper(str.substr(lastPos, pos - lastPos));

		if(isDll)
		{
			string path64 = path;
			size_t index = path64.find("SYSTEM32");
			if(index != string::npos)
				path64.replace(index, 8, "SYSWOW64");
			FindFileInPathInner(res, path64, filename);
		}
		FindFileInPathInner(res, path, filename);
		
		lastPos = str.find_first_not_of(delimiters, pos);
		pos = str.find_first_of(delimiters, lastPos);
	}

	return res;
}

static string GetEnv(const char* varname) {
	char* buff = NULL;
	size_t len = 0;
	if(_dupenv_s(&buff, &len, varname)) {
		return "";
	}
	else {
		if(buff == NULL) {
			return "";
		}
		string s = buff;
		free(buff);
		return s;
	}
}

static std::map<string, MemoryFile*> dllFileMap;
const char *LoadDLL(const char *name) {
	string strName = ToUpper(name);
	if(!EndsWith(strName.c_str(), ".DLL"))
		strName += ".DLL";

	auto it = dllFileMap.find(strName);
	if(it != dllFileMap.end())
		return it->second->GetPtr();
	
	vector<string> filepaths = FindFileInPath(strName.c_str(), GetEnv("PATH").c_str(), true);
	if(filepaths.empty())
	{
		Log::Error("", "Cannot find DLL '%s'", strName.c_str());
		return NULL;
	}

	MemoryFile* mf = new MemoryFile(filepaths[0].c_str());
	dllFileMap[strName] = mf;
	const char* module = mf->GetPtr();

	const IMAGE_DOS_HEADER* pDH = (const PIMAGE_DOS_HEADER)module;
	const IMAGE_NT_HEADERS32* pNTH = (const PIMAGE_NT_HEADERS32)(module + pDH->e_lfanew);

	const DWORD exportRVA = pNTH->OptionalHeader.DataDirectory[0].VirtualAddress;
	if (exportRVA == 0) {
		Log::Error("", "Missing export table in '%s'\n\n"
			"If running under Wine, copy all imported DLL files from a real Windows to your Wine path.", strName.c_str());
	}

	return module;
}

static bool RunExecutable(const char* filename) {
	char args[MAX_PATH];
	strcpy_s(args, GetCommandLine());

	STARTUPINFO siStartupInfo = {};
	PROCESS_INFORMATION piProcessInfo = {};
	siStartupInfo.cb = sizeof(siStartupInfo);

	if(!CreateProcess(filename,		// LPCSTR lpApplicationName
		args,						// LPSTR lpCommandLine,
		NULL,						// LPSECURITY_ATTRIBUTES lpProcessAttributes,
		NULL,						// LPSECURITY_ATTRIBUTES lpThreadAttributes,
		FALSE,						// BOOL bInheritHandles,
		CREATE_DEFAULT_ERROR_MODE,	// DWORD dwCreationFlags,
		NULL,						// LPVOID lpEnvironment,
		NULL,						// LPCSTR lpCurrentDirectory,
		&siStartupInfo,				// LPSTARTUPINFOA lpStartupInfo,
		&piProcessInfo))			// LPPROCESS_INFORMATION lpProcessInformation
	{
		return false;
	}

	// Wait until application has terminated
	WaitForSingleObject(piProcessInfo.hProcess, INFINITE);

	// Close process and thread handles
	CloseHandle(piProcessInfo.hThread);
	CloseHandle(piProcessInfo.hProcess);
	return true;
}

static void ParseExports(CmdParamMultiAssign& arg, Crinkler& crinkler) {
	while (arg.HasNext()) {
		Export e = ParseExport(arg.GetValue1(), arg.GetValue2());
		crinkler.AddExport(std::move(e));
		arg.Next();
	}
}

static void RunOriginalLinker(const char* linkerName) {
	vector<string> res = FindFileInPath(linkerName, GetEnv("PATH").c_str(), false);
	const char* needle = "Crinkler";
	const int needleLength = (int)strlen(needle);

	for(const string& str : res) {
		MemoryFile mf(str.c_str());
		bool isCrinkler = false;
		for(int i = 0; i < mf.GetSize()-needleLength; i++) {
			if(memcmp(mf.GetPtr()+i, needle, needleLength) == 0) {
				isCrinkler = true;
				break;
			}
		}

		if(!isCrinkler) {
			// Run linker
			printf("Launching default linker at '%s'\n\n", str.c_str());
			fflush(stdout);
			if(!RunExecutable(str.c_str()))
				Log::Error("", "Failed to launch default linker, errorcode: %X", GetLastError());
			return;
		}
	}

	// Linker not found
	Log::Error("", "Cannot find default linker '%s' in path", linkerName);
}

const int TRANSFORM_CALLS = 0x01;
int main(int argc, char* argv[]) {
	int time1 = GetTickCount();

	// Find canonical name of the Crinkler executable
	char crinklerCanonicalName[1024];
	{
		char tmp[1024];
		GetModuleFileName(NULL, tmp, sizeof(tmp));
		GetFullPathName(tmp, sizeof(crinklerCanonicalName), crinklerCanonicalName, NULL);
	}

	EnableMiniDumps();

	string crinklerFilename = StripPath(crinklerCanonicalName);
	
	// Command line parameters
	CmdParamInt hashsizeArg("HASHSIZE", "number of megabytes for hashing", "size in mb", PARAM_SHOW_CONSTRAINTS,
							1, 1000, 500);
	CmdParamInt hashtriesArg("HASHTRIES", "number of hashing tries", "number of hashing tries", 0,
							0, 100000, 100);
	CmdParamInt hunktriesArg("ORDERTRIES", "", "number of section reordering tries", 0,
							0, 100000, 0);
	CmdParamInt truncateFloatsArg("TRUNCATEFLOATS", "truncates floats", "bits", PARAM_ALLOW_NO_ARGUMENT_DEFAULT,
							0, 64, 64);
	CmdParamInt overrideAlignmentsArg("OVERRIDEALIGNMENTS", "override section alignments using align labels", "bits",  PARAM_ALLOW_NO_ARGUMENT_DEFAULT,
							0, 30, -1);
	CmdParamSwitch unalignCodeArg("UNALIGNCODE", "force alignment of code sections to 1", 0);
	CmdParamSwitch noDefaultLibArg("NODEFAULTLIB", "Do not implicitly link to runtime library", 0);
	CmdParamString entryArg("ENTRY", "name of the entrypoint", "symbol",
						PARAM_IS_SWITCH|PARAM_FORBID_MULTIPLE_DEFINITIONS, "");
	CmdParamString outArg("OUT", "output filename", "filename", 
						PARAM_IS_SWITCH|PARAM_FORBID_MULTIPLE_DEFINITIONS, "out.exe");
	CmdParamString summaryArg("REPORT", "report html filename", "filename", 
						PARAM_IS_SWITCH|PARAM_FORBID_MULTIPLE_DEFINITIONS, "");
	CmdParamString reuseFileArg("REUSE", "reuse html filename", "filename",
		PARAM_IS_SWITCH | PARAM_FORBID_MULTIPLE_DEFINITIONS, "");
	CmdParamFlags reuseArg("REUSEMODE", "select reuse mode", PARAM_FORBID_MULTIPLE_DEFINITIONS, REUSE_STABLE,
		"OFF", REUSE_OFF, "WRITE", REUSE_WRITE, "IMPROVE", REUSE_IMPROVE, "STABLE", REUSE_STABLE, NULL);
	CmdParamSwitch helpFlag("?", "help", 0);
	CmdParamSwitch crinklerFlag("CRINKLER", "enables Crinkler", 0);
	CmdParamSwitch recompressFlag("RECOMPRESS", "recompress a Crinkler file", 0);
	CmdParamSwitch unsafeImportArg("UNSAFEIMPORT", "crash if a DLL is missing", 0);
	CmdParamSwitch showProgressArg("PROGRESSGUI", "show a graphical progress bar", 0);

	CmdParamSwitch tinyHeader("TINYHEADER", "use tiny header", 0);
	CmdParamSwitch tinyImport("TINYIMPORT", "use tiny import", 0);
	CmdParamFlags subsystemArg("SUBSYSTEM", "select subsystem", PARAM_FORBID_MULTIPLE_DEFINITIONS, SUBSYSTEM_CONSOLE, 
						"WINDOWS", SUBSYSTEM_WINDOWS, "CONSOLE", SUBSYSTEM_CONSOLE, NULL);
	CmdParamFlags largeAddressAwareArg("LARGEADDRESSAWARE", "allow addresses beyond 2gb", PARAM_ALLOW_NO_ARGUMENT_DEFAULT | PARAM_FORBID_MULTIPLE_DEFINITIONS, 1, "NO", 0, NULL);
	CmdParamFlags priorityArg("PRIORITY", "select priority", PARAM_FORBID_MULTIPLE_DEFINITIONS, BELOW_NORMAL_PRIORITY_CLASS, 
						"IDLE", IDLE_PRIORITY_CLASS, "BELOWNORMAL", BELOW_NORMAL_PRIORITY_CLASS, "NORMAL", NORMAL_PRIORITY_CLASS, NULL);
	CmdParamFlags compmodeArg("COMPMODE", "compression mode", PARAM_FORBID_MULTIPLE_DEFINITIONS, COMPRESSION_SLOW,
						"INSTANT", COMPRESSION_INSTANT, 
						"FAST", COMPRESSION_FAST, 
						"SLOW", COMPRESSION_SLOW,
						"VERYSLOW", COMPRESSION_VERYSLOW, NULL);
	CmdParamFlags printArg("PRINT", "print", 0, 0, 
							"LABELS", PRINT_LABELS, "IMPORTS", PRINT_IMPORTS,
							"MODELS", PRINT_MODELS, 
							NULL);
	CmdParamFlags transformArg("TRANSFORM", "select transformations", 0, 0, 
							"CALLS", TRANSFORM_CALLS,
							NULL);
	CmdParamFlags saturateArg("SATURATE", "saturate counters (for highly repetitive data)", PARAM_ALLOW_NO_ARGUMENT_DEFAULT | PARAM_FORBID_MULTIPLE_DEFINITIONS, 1, "NO", 0, NULL);
	CmdParamString libpathArg("LIBPATH", "adds a path to the library search path", "dirs", PARAM_IS_SWITCH, 0);
	CmdParamString rangeImportArg("RANGE", "use range importing for this dll", "dllname", PARAM_IS_SWITCH, 0);
	CmdParamMultiAssign replaceDllArg("REPLACEDLL", "replace a dll with another", "oldDLL=newDLL", PARAM_IS_SWITCH);
	CmdParamMultiAssign fallbackDllArg("FALLBACKDLL", "try opening another dll if the first one fails", "firstDLL=otherDLL", PARAM_IS_SWITCH);
	CmdParamMultiAssign exportArg("EXPORT", "export value by name", "name=value/label", PARAM_IS_SWITCH | PARAM_ALLOW_MISSING_VALUE);
	CmdParamSwitch stripExportsArg("STRIPEXPORTS", "remove exports from executable", 0);
	CmdParamSwitch noInitializersArg("NOINITIALIZERS", "do not run dynamic initializers", 0);
	CmdParamString filesArg("FILES", "list of filenames", "", PARAM_HIDE_IN_PARAM_LIST, 0);
	CmdLineInterface cmdline(CRINKLER_TITLE, CMDI_PARSE_FILES);

	cmdline.AddParams(&helpFlag, &crinklerFlag, &hashsizeArg, &hashtriesArg, &hunktriesArg, &noDefaultLibArg, &entryArg, &outArg, &summaryArg, &reuseFileArg, &reuseArg, &unsafeImportArg,
						&subsystemArg, &largeAddressAwareArg, &truncateFloatsArg, &overrideAlignmentsArg, &unalignCodeArg, &compmodeArg, &saturateArg, &printArg, &transformArg, &libpathArg, 
						&rangeImportArg, &replaceDllArg, &fallbackDllArg, &exportArg, &stripExportsArg, &noInitializersArg, &filesArg, &priorityArg, &showProgressArg, &recompressFlag,
						&tinyHeader, &tinyImport,
						NULL);
	

	// Print syntax?
	if(!cmdline.SetCmdParameters(argc, argv) || argc == 1 || cmdline.RemoveToken("/?")) {
		cmdline.PrintSyntax();
		return 0;
	}

	cmdline.PrintHeader();
	fflush(stdout);

	// Run default linker or Crinkler?
	if(!cmdline.RemoveToken("/CRINKLER") && ToUpper(crinklerFilename).compare("CRINKLER.EXE") != 0) {
		RunOriginalLinker(crinklerFilename.c_str());
		return 0;
	}

	// Set priority
	SetPriorityClass(GetCurrentProcess(), priorityArg.GetValue());

	Crinkler crinkler;

	// Recompress
	if(cmdline.RemoveToken("/RECOMPRESS")) {
		CmdLineInterface cmdline2(CRINKLER_TITLE, CMDI_PARSE_FILES);
		outArg.SetDefault("*dummy*");
		hashsizeArg.SetDefault(-1);
		subsystemArg.SetDefault(-1);
		compmodeArg.SetDefault(-1);

		cmdline2.AddParams(&crinklerFlag, &recompressFlag, &outArg, &hashsizeArg, &hashtriesArg, &subsystemArg, &largeAddressAwareArg, &compmodeArg, &saturateArg, &replaceDllArg, &summaryArg, &exportArg, &stripExportsArg, &priorityArg, &showProgressArg, &filesArg, NULL);
		cmdline2.SetCmdParameters(argc, argv);
		if(cmdline2.Parse()) {
			crinkler.SetHashsize(hashsizeArg.GetValue());
			crinkler.SetSubsystem((SubsystemType)subsystemArg.GetValue());
			crinkler.SetLargeAddressAware(largeAddressAwareArg.GetValueIfPresent(-1));
			crinkler.SetCompressionType((CompressionType)compmodeArg.GetValue());
			crinkler.SetSaturate(saturateArg.GetValueIfPresent(-1));
			crinkler.SetHashtries(hashtriesArg.GetValue());
			crinkler.ShowProgressBar(showProgressArg.GetValue());
			crinkler.SetSummary(summaryArg.GetValue());
			ParseExports(exportArg, crinkler);
			crinkler.SetStripExports(stripExportsArg.GetValue());

			IdentityTransform identTransform;
			crinkler.SetTransform(&identTransform);

			const char* infilename = filesArg.GetValue();
			filesArg.Next();
			if(infilename == NULL || filesArg.HasNext()) {
				printf("%s\n", filesArg.GetValue());
				Log::Error("", "Crinkler recompression takes exactly one file argument");
				return 1;
			}

			const char* outfilename = outArg.GetValue();
			if (strcmp(outfilename, "*dummy*") == 0) {
				outfilename = infilename;
			}

			printf("Source: %s\n", infilename);
			printf("Target: %s\n", outfilename);
			if(subsystemArg.GetValue() == -1) {
				printf("Subsystem type: Inherited from original\n");
			} else {
				printf("Subsystem type: %s\n", subsystemArg.GetValue() == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
			}
			if(largeAddressAwareArg.GetNumMatches() == 0) {
				printf("Large address aware: Inherited from original\n");
			} else {
				printf("Large address aware: %s\n", largeAddressAwareArg.GetValue() ? "YES" : "NO");
			}
			if(compmodeArg.GetValue() == -1) {
				printf("Compression mode: Models inherited from original\n");
			} else {
				printf("Compression mode: %s\n", CompressionTypeName((CompressionType)compmodeArg.GetValue()));
			}
			if (saturateArg.GetNumMatches() == 0) {
				printf("Saturate counters: Inherited from original\n");
			} else {
				printf("Saturate counters: %s\n", saturateArg.GetValue() ? "YES" : "NO");
			}
			if (hashsizeArg.GetValue() == -1) {
				printf("Hash size: Inherited from original\n");
			} else {
				printf("Hash size: %d MB\n", hashsizeArg.GetValue());
				printf("Hash tries: %d\n", hashtriesArg.GetValue());
			}
			printf("Report: %s\n", strlen(summaryArg.GetValue()) > 0 ? summaryArg.GetValue() : "NONE");

			// Replace DLL
			{
				printf("Replace DLLs: ");
				if (!replaceDllArg.HasNext())
					printf("NONE");

				bool legal = true;
				bool first = true;
				while (replaceDllArg.HasNext()) {
					if (!first) printf(", ");
					printf("%s -> %s", replaceDllArg.GetValue1(), replaceDllArg.GetValue2());

					if (strlen(replaceDllArg.GetValue1()) != strlen(replaceDllArg.GetValue2()))
						legal = false;
					crinkler.AddReplaceDll(replaceDllArg.GetValue1(), replaceDllArg.GetValue2());
					replaceDllArg.Next();
					first = false;
				}
				printf("\n");
				if (!legal) {
					Log::Error("", "In recompression, a DLL can only be replaced by one with the same length.");
				}
			}

			printf("Exports: ");
			if (exportArg.GetNumMatches() == 0) {
				if (stripExportsArg.GetValue()) {
					printf("Strip away\n");
				} else {
					printf("Keep from original\n");
				}
			} else {
				if (stripExportsArg.GetValue()) {
					printf("Strip and replace by:\n");
				} else {
					printf("Keep and supplement by:\n");
				}
				PrintExports(crinkler.GetExports());
			}
			printf("\n");

			crinkler.Recompress(infilename, outfilename);
			return 0;
		}

		return 1;
	}

	if(!cmdline.Parse()) {
		return 1;
	}

	if (stripExportsArg.GetValue()) {
		Log::Error("", "Export stripping can only be performed during recompression.");
	}

	if (transformArg.GetValue() != 0 && tinyHeader.GetValue())
	{
		Log::Warning("", "Transforms are not supported with /TINYHEADER.");
		transformArg.m_value = 0;
	}

	// Set Crinkler options
	crinkler.SetUseTinyHeader(tinyHeader.GetValue());
	crinkler.SetUseTinyImport(tinyImport.GetValue());
	crinkler.SetImportingType(!unsafeImportArg.GetValue());
	crinkler.SetEntry(entryArg.GetValue());
	crinkler.SetHashsize(hashsizeArg.GetValue());
	crinkler.SetSubsystem((SubsystemType)subsystemArg.GetValue());
	crinkler.SetLargeAddressAware(largeAddressAwareArg.GetValueIfPresent(0));
	crinkler.SetCompressionType((CompressionType)compmodeArg.GetValue());
	crinkler.SetHashtries(hashtriesArg.GetValue());
	crinkler.SetHunktries(hunktriesArg.GetValue());
	crinkler.SetSaturate(saturateArg.GetValueIfPresent(0));
	crinkler.SetPrintFlags(printArg.GetValue());
	crinkler.ShowProgressBar(showProgressArg.GetValue());
	crinkler.SetTruncateFloats(truncateFloatsArg.GetNumMatches() > 0);
	crinkler.SetTruncateBits(truncateFloatsArg.GetValue());
	crinkler.SetOverrideAlignments(overrideAlignmentsArg.GetNumMatches() > 0);
	crinkler.SetUnalignCode(unalignCodeArg.GetValue());
	crinkler.SetAlignmentBits(overrideAlignmentsArg.GetValue());
	crinkler.SetRunInitializers(!noInitializersArg.GetValue());
	crinkler.SetSummary(summaryArg.GetValue());
	if (reuseFileArg.GetNumMatches() > 0) {
		crinkler.SetReuse((ReuseType)reuseArg.GetValue(), reuseFileArg.GetValue());
	}
	ParseExports(exportArg, crinkler);


	// Transforms
	IdentityTransform identTransform;
	CallTransform callTransform;
	if(transformArg.GetValue() & TRANSFORM_CALLS)
		crinkler.SetTransform(&callTransform);
	else
		crinkler.SetTransform(&identTransform);


	// Print some info
	printf("Target: %s\n", outArg.GetValue());
	printf("Tiny compressor: %s\n", tinyHeader.GetValue() ? "YES" : "NO");
	printf("Tiny import: %s\n", tinyImport.GetValue() ? "YES" : "NO");
	printf("Subsystem type: %s\n", subsystemArg.GetValue() == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
	printf("Large address aware: %s\n", largeAddressAwareArg.GetValueIfPresent(0) ? "YES" : "NO");
	printf("Compression mode: %s\n", CompressionTypeName((CompressionType)compmodeArg.GetValue()));
	printf("Saturate counters: %s\n", saturateArg.GetValueIfPresent(0) ? "YES" : "NO");
	printf("Hash size: %d MB\n", hashsizeArg.GetValue());
	printf("Hash tries: %d\n", hashtriesArg.GetValue());
	printf("Order tries: %d\n", hunktriesArg.GetValue());
	if (reuseFileArg.GetNumMatches() > 0) {
		printf("Reuse mode: %s\n", ReuseTypeName((ReuseType)reuseArg.GetValue()));
		printf("Reuse file: %s\n", reuseFileArg.GetValue());
	}
	else {
		printf("Reuse mode: OFF (no file specified)\n");
	}
	printf("Report: %s\n", strlen(summaryArg.GetValue()) > 0 ? summaryArg.GetValue() : "NONE");
	printf("Transforms: %s\n", (transformArg.GetValue() & TRANSFORM_CALLS) ? "CALLS" : "NONE");

	// Replace DLL
	{
		printf("Replace DLLs: ");
		if(!replaceDllArg.HasNext())
			printf("NONE");
		
		bool first = true;
		while(replaceDllArg.HasNext()) {
			if (!first) printf(", ");
			printf("%s -> %s", replaceDllArg.GetValue1(), replaceDllArg.GetValue2());
			
			crinkler.AddReplaceDll(replaceDllArg.GetValue1(), replaceDllArg.GetValue2());
			replaceDllArg.Next();
			first = false;
		}
		printf("\n");
	}

	// Fallback DLL
	{
		printf("Fallback DLLs: ");
		if (!fallbackDllArg.HasNext())
			printf("NONE");

		bool first = true;
		while (fallbackDllArg.HasNext()) {
			if (!first) printf(", ");
			printf("%s -> %s", fallbackDllArg.GetValue1(), fallbackDllArg.GetValue2());

			crinkler.AddFallbackDll(fallbackDllArg.GetValue1(), fallbackDllArg.GetValue2());
			fallbackDllArg.Next();
			first = false;
		}
		printf("\n");
	}

	// Range
	{
		printf("Range DLLs: ");
		if(!rangeImportArg.HasNext())
			printf("NONE");

		bool first = true;
		while(rangeImportArg.HasNext()) {
			if (!first) printf(", ");
			printf("%s", rangeImportArg.GetValue());

			crinkler.AddRangeDll(rangeImportArg.GetValue());
			rangeImportArg.Next();
			first = false;
		}
		printf("\n");
	}

	// Exports
	{
		printf("Exports:");
		auto exports = crinkler.GetExports();
		if (exports.empty()) {
			printf(" NONE\n");
		}
		else {
			printf("\n");
			PrintExports(exports);
		}
	}
	printf("\n");

	// Build search path
	string lib = "";
	char drive[3] = "?:";
	drive[0] = (char) (_getdrive()+'A'-1);
	lib += string(drive) + ";";
	
	while(libpathArg.HasNext()) {
		lib += libpathArg.GetValue();
		lib += ";";
		libpathArg.Next();
	}
	lib += ";" + GetEnv("LIB");
	lib += ";" + GetEnv("PATH");
	
	// Load files
	{
		while(filesArg.HasNext()) {
			const char* filename = filesArg.GetValue();
			filesArg.Next();
			vector<string> res = FindFileInPath(filename, lib.c_str(), false);
			if(res.size() == 0) {
				Log::Error(filename, "Cannot open file '%s'\n", filename);
				return -1;
			} else {
				printf("Loading %s...\n", filename);
				fflush(stdout);
				string filepath = *res.begin();
				crinkler.Load(filepath.c_str());
			}
		}
		if (!noDefaultLibArg.GetValue()) {
			crinkler.AddRuntimeLibrary();
		}
		printf("\n");
	}

	printf("Linking...\n\n");
	fflush(stdout);
	crinkler.Link(outArg.GetValue());

	int time2 = GetTickCount();
	int time = (time2-time1+500)/1000;
	printf("time spent: %dm%02ds\n", time/60, time%60);
}
