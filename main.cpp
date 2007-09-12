#include <windows.h>
#include <cstdio>
#include <iostream>
#include <list>
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
#include "IdentityTransform.h"
#include "Fix.h"
#include "MemoryFile.h"

using namespace std;

static bool fileExists(const char* filename) {
	FILE* file;
	fopen_s(&file, filename, "r");
	if(file != NULL) {
		fclose(file);
		return true;
	}
	return false;
}

//returns a list of found files in uppercase
static list<string> findFileInPath(const char* filename, const char* path) {
	list<string> res;
	string str = path;

	string delimiters = ";";
	string::size_type lastPos = str.find_first_not_of(delimiters, 0);
	string::size_type pos     = str.find_first_of(delimiters, lastPos);

	while (string::npos != pos || string::npos != lastPos)
	{
		// Found a token, add it to the vector.
		string token = str.substr(lastPos, pos - lastPos);
		if(token[token.size()-1] != '\\' && token[token.size()-1] != '/')
			token += "\\";
		token += filename;

		char canonicalName[1024];
		GetFullPathName(
			token.c_str(),
			sizeof(canonicalName),
			canonicalName,
			NULL
			);

		if(fileExists(canonicalName)) {
			res.push_back(toUpper(canonicalName));
		}
		
		lastPos = str.find_first_not_of(delimiters, pos);
		pos = str.find_first_of(delimiters, lastPos);
	}

	return res;
}

static string getEnv(const char* varname) {
	char* buff = NULL;
	size_t len = 0;
	if(_dupenv_s(&buff, &len, varname)) {
		return "";
	} else {
		if(buff == NULL) {
			return "";
		}
		string s = buff;
		free(buff);
		return s;
	}
}

static bool runExecutable(const char* filename) {
	char args[MAX_PATH];
	strcpy_s(args, GetCommandLine());

	STARTUPINFO siStartupInfo;
	PROCESS_INFORMATION piProcessInfo;
	memset(&siStartupInfo, 0, sizeof(siStartupInfo));
	memset(&piProcessInfo, 0, sizeof(piProcessInfo));
	siStartupInfo.cb = sizeof(siStartupInfo);

	if(!CreateProcess(filename,		//LPCSTR lpApplicationName
		args,	//LPSTR lpCommandLine,
		NULL,			//LPSECURITY_ATTRIBUTES lpProcessAttributes,
		NULL,			//LPSECURITY_ATTRIBUTES lpThreadAttributes,
		FALSE,			//BOOL bInheritHandles,
		CREATE_DEFAULT_ERROR_MODE,//DWORD dwCreationFlags,
		NULL,			//LPVOID lpEnvironment,
		NULL,			//LPCSTR lpCurrentDirectory,
		&siStartupInfo, //LPSTARTUPINFOA lpStartupInfo,
		&piProcessInfo))//LPPROCESS_INFORMATION lpProcessInformation
	{
		return false;
	}

	//Wait until application has terminated
	WaitForSingleObject(piProcessInfo.hProcess, INFINITE);

	//Close process and thread handles
	CloseHandle(piProcessInfo.hThread);
	CloseHandle(piProcessInfo.hProcess);
	return true;
}

static void runOriginalLinker(const char* linkerName) {
	string path = ".;" + getEnv("PATH");
	list<string> res = findFileInPath(linkerName, path.c_str());
	const char* needle = "Crinkler";
	const int needleLength = strlen(needle);

	for(list<string>::const_iterator it = res.begin(); it != res.end(); it++) {
		MemoryFile mf(it->c_str());
		bool isCrinkler = false;
		for(int i = 0; i < mf.getSize()-needleLength; i++) {
			if(memcmp(mf.getPtr()+i, needle, needleLength) == 0) {
				isCrinkler = true;
				break;
			}
		}

		if(!isCrinkler) {
			//run linker
			printf("Launching default linker at '%s'\n\n", it->c_str());
			fflush(stdout);
			if(!runExecutable(it->c_str()))
				Log::error(0, "", "failed to launch default linker, errorcode: %X", GetLastError());
			return;
		}
	}

	//Linker not found
	Log::error(0, "", "could not find default linker '%s' in path", linkerName);
}

#define TRANSFORM_CALLS		0x01
#define TRANSFORM_CALLS2	0x02

int main(int argc, char* argv[]) {	
	//find canonical name of the crinkler executable
	char crinklerCanonicalName[1024];
	{
		char tmp[1024];
		GetModuleFileName(NULL, tmp, sizeof(tmp));
		GetFullPathName(tmp, sizeof(crinklerCanonicalName), crinklerCanonicalName, NULL);
	}
	
	string crinklerFilename = stripPath(crinklerCanonicalName);
	
	//cmdline parameters
	CmdParamInt hashsizeArg("HASHSIZE", "number of megabytes for hashing", "size in mb", PARAM_SHOW_CONSTRAINTS,
							10, 1000, 100);
	CmdParamInt hashtriesArg("HASHTRIES", "number of hashing tries", "number of hashing tries", 0,
							0, 10000, 20);
	CmdParamInt hunktriesArg("ORDERTRIES", "", "number of section reordering tries", 0,
							0, 10000, 0);
	CmdParamInt truncateFloats("TRUNCATEFLOATS", "truncates floats", "bits", PARAM_ALLOW_NO_ARGUMENT_DEFAULT,
							0, 32, 32);
	CmdParamString entryArg("ENTRY", "name of the entrypoint", "symbol", 
						PARAM_IS_SWITCH|PARAM_FORBID_MULTIPLE_DEFINITIONS, "");
	CmdParamString outArg("OUT", "output filename", "filename", 
						PARAM_IS_SWITCH|PARAM_FORBID_MULTIPLE_DEFINITIONS, "out.exe");
	CmdParamString summaryArg("REPORT", "report html filename", "filename", 
						PARAM_IS_SWITCH|PARAM_FORBID_MULTIPLE_DEFINITIONS, "");
	CmdParamSwitch crinklerFlag("CRINKLER", "enables crinkler", 0);
	CmdParamSwitch fixFlag("FIX", "fix old crinkler files", 0);
	CmdParamSwitch unsafeImportArg("UNSAFEIMPORT", "crash if a dll is missing", 0);
	CmdParamSwitch showProgressArg("PROGRESSGUI", "shows a progressbar", 0);
	CmdParamSwitch tinyCompressor("1K", "1k mode", PARAM_HIDE_IN_PARAM_LIST);
	CmdParamFlags subsystemArg("SUBSYSTEM", "select subsystem", PARAM_FORBID_MULTIPLE_DEFINITIONS, SUBSYSTEM_CONSOLE, 
						"WINDOWS", SUBSYSTEM_WINDOWS, "CONSOLE", SUBSYSTEM_CONSOLE, NULL);
	CmdParamFlags priorityArg("PRIORITY", "select priority", PARAM_FORBID_MULTIPLE_DEFINITIONS, BELOW_NORMAL_PRIORITY_CLASS, 
						"IDLE", IDLE_PRIORITY_CLASS, "BELOWNORMAL", BELOW_NORMAL_PRIORITY_CLASS, "NORMAL", NORMAL_PRIORITY_CLASS, NULL);
	CmdParamFlags compmodeArg("COMPMODE", "compression mode", PARAM_FORBID_MULTIPLE_DEFINITIONS, COMPRESSION_FAST,
						"INSTANT", COMPRESSION_INSTANT, 
						"FAST", COMPRESSION_FAST, 
						"SLOW", COMPRESSION_SLOW, NULL);
	CmdParamFlags verboseArg("PRINT", "print", 0, 0, 
							"LABELS", VERBOSE_LABELS, "IMPORTS", VERBOSE_IMPORTS,
							"MODELS", VERBOSE_MODELS, "FUNCTIONS", VERBOSE_FUNCTIONS,
							"FUNCTIONS-BYNAME", VERBOSE_FUNCTIONS_BYNAME,
							"FUNCTIONS-BYSIZE", VERBOSE_FUNCTIONS_BYSIZE,
							NULL);
	CmdParamFlags transformArg("TRANSFORM", "select transformations", 0, 0, 
							"CALLS", TRANSFORM_CALLS,
							NULL);
	CmdParamString libpathArg("LIBPATH", "adds a path to the library search path", "dirs", PARAM_IS_SWITCH, 0);
	CmdParamString rangeImportArg("RANGE", "use range importing for this dll", "dllname", PARAM_IS_SWITCH, 0);
	CmdParamMultiAssign replaceDllArg("REPLACEDLL", "replace a dll with another", "oldDLL=newDLL", PARAM_IS_SWITCH);
	CmdParamString filesArg("FILES", "list of filenames", "", PARAM_HIDE_IN_PARAM_LIST, 0);
	CmdLineInterface cmdline(CRINKLER_TITLE, CMDI_PARSE_FILES);

	cmdline.addParams(&crinklerFlag, &hashsizeArg, &hashtriesArg, &hunktriesArg, &entryArg, &outArg, &summaryArg, &unsafeImportArg,
						&subsystemArg, &truncateFloats, &compmodeArg, &verboseArg, &transformArg, &libpathArg, 
						&rangeImportArg, &replaceDllArg, &filesArg, &priorityArg, &showProgressArg, 
						&tinyCompressor,	//TODO: remove before release
						NULL);
	cmdline.setCmdParameters(argc, argv);

	//print syntax?
	if(argc == 1) {
		cmdline.printSyntax();
		return 0;
	}

	cmdline.printHeader();
	fflush(stdout);

	//Run default linker or crinkler?
	if(!cmdline.removeToken("/CRINKLER") && toUpper(crinklerFilename).compare("CRINKLER.EXE") != 0) {
		runOriginalLinker(crinklerFilename.c_str());
		return 0;
	}

	//Fix mode
	if(cmdline.removeToken("/FIX")) {
		CmdLineInterface cmdline2(CRINKLER_TITLE, CMDI_PARSE_FILES);

		cmdline2.addParams(&fixFlag, &outArg, &filesArg, NULL);
		cmdline2.setCmdParameters(argc, argv);
		if(cmdline2.parse()) {
			const char* infilename = filesArg.getValue();
			if(infilename == NULL || filesArg.hasNext() != 0) {
				Log::error(0, "", "Crinkler fix takes exactly one file argument");
				return 1;
			}

			FixFile(infilename, outArg.getValue());
			return 0;
		}

		return 1;
	}

	if(!cmdline.parse()) {
		return 1;
	}


	//set priority
	SetPriorityClass(GetCurrentProcess(), priorityArg.getValue());

	Crinkler crinkler;

	//set crinkler options
	crinkler.set1KMode(tinyCompressor.getValue());
	crinkler.setImportingType(!unsafeImportArg.getValue());
	crinkler.setEntry(entryArg.getValue());
	crinkler.setSummary(summaryArg.getValue());
	crinkler.setHashsize(hashsizeArg.getValue());
	crinkler.setSubsystem((SubsystemType)subsystemArg.getValue());
	crinkler.setCompressionType((CompressionType)compmodeArg.getValue());
	crinkler.setHashtries(hashtriesArg.getValue());
	crinkler.setHunktries(hunktriesArg.getValue());
	crinkler.setVerboseFlags(verboseArg.getValue());
	crinkler.showProgressBar(showProgressArg.getValue());
	crinkler.setTruncateFloats(truncateFloats.getNumMatches() > 0);
	crinkler.setTruncateBits(truncateFloats.getValue());

	//transforms
	IdentityTransform identTransform;
	CallTransform callTransform;
	if(transformArg.getValue() & TRANSFORM_CALLS)
		crinkler.setTransform(&callTransform);
	else
		crinkler.setTransform(&identTransform);

	//print some info
	printf("Target: %s\n", outArg.getValue());
	printf("Subsystem type: %s\n", subsystemArg.getValue() == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
	printf("Compression mode: ");
	switch(compmodeArg.getValue()) {
		case COMPRESSION_INSTANT:
			printf("INSTANT\n");
			break;
		case COMPRESSION_FAST:
			printf("FAST\n");
			break;
		case COMPRESSION_SLOW:
			printf("SLOW\n");
			break;
	}
	printf("Hash size: %d MB\n", hashsizeArg.getValue());
	printf("Hash tries: %d\n", hashtriesArg.getValue());
	printf("Order tries: %d\n", hunktriesArg.getValue());
	printf("Report: %s\n", strlen(summaryArg.getValue()) > 0 ? summaryArg.getValue() : "NONE");
	printf("Transforms: %s\n", (transformArg.getValue() & TRANSFORM_CALLS) ? "CALLS" : "NONE");

	//replace dll
	{
		printf("Replace DLLs: ");
		if(!replaceDllArg.hasNext())
			printf("NONE");
		
		bool first = true;
		while(replaceDllArg.hasNext()) {
			if(first)
				printf("%s -> %s", replaceDllArg.getValue1(), replaceDllArg.getValue2());
			else
				printf(", %s -> %s", replaceDllArg.getValue1(), replaceDllArg.getValue2());
			
			crinkler.addReplaceDll(replaceDllArg.getValue1(), replaceDllArg.getValue2());
			first = false;	
			replaceDllArg.next();
		}
		printf("\n");
	}

	//range
	{
		printf("Range DLLs: ");
		if(!rangeImportArg.hasNext())
			printf("NONE");

		bool first = true;
		while(rangeImportArg.hasNext()) {
			if(first)
				printf("%s", rangeImportArg.getValue());
			else
				printf(", %s", rangeImportArg.getValue());

			crinkler.addRangeDll(rangeImportArg.getValue());
			rangeImportArg.next();
			first = false;
		}
		printf("\n");
	}
	printf("\n");

	//build search library+object search path
	string lib = ";.;";
	char drive[3] = "?:";
	drive[0] = (char) (_getdrive()+'A'-1);
	lib += string(drive) + ";";
	
	while(libpathArg.hasNext()) {
		lib += libpathArg.getValue();
		lib += ";";
		libpathArg.next();
	}
	lib += ";" + getEnv("LIB");
	lib += ";" + getEnv("PATH");

	//load files
	{
		printf("loading: \n");
		int c = 0;
		while(filesArg.hasNext()) {
			const char* filename = filesArg.getValue();
			filesArg.next();
			list<string> res = findFileInPath(filename, lib.c_str());
			if(res.size() == 0) {
				Log::error(1104, "", "cannot open file '%s'", filename);
				return -1;
			} else {
				printf("  %s", filename);
				if((c++ % 4) == 3)
					printf("\n");
				string filepath = *res.begin();
				crinkler.load(filepath.c_str());
			}
		}
		printf("\n");
	}

	crinkler.link(outArg.getValue());
}
