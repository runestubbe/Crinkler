#ifdef _DEBUG
#include <vld.h>
#endif
#include <cstdio>
#include <windows.h>
#include <iostream>
#include <list>
#include <string>

#include "CoffObjectLoader.h"
#include "CoffLibraryLoader.h"
#include "HunkList.h"
#include "Hunk.h"
#include "Crinkler.h"
#include "CmdLineInterface.h"
#include "Log.h"
#include "StringMisc.h"

using namespace std;

bool fileExists(const char* filename) {
	FILE* file;
	fopen_s(&file, filename, "r");
	if(file != NULL) {
		fclose(file);
		return true;
	}
	return false;
}

list<string> findFileInPath(const char* filename, const char* path) {
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
			res.push_back(canonicalName);
		}
		
		lastPos = str.find_first_not_of(delimiters, pos);
		pos = str.find_first_of(delimiters, lastPos);
	}

	return res;
}

string getEnv(const char* varname) {
	char* buff;
	size_t len;
	if(_dupenv_s(&buff, &len, varname)) {
		return "";
	} else {
		string s = buff;
		free(buff);
		return s;
	}
}

void runOriginalLinker(const char* crinklerCanonicalName) {
	//Crinkler not enabled. Search for linker
	printf("Crinkler not enabled\n");

	string path = ".;" + getEnv("PATH");
	list<string> res = findFileInPath("link.exe", path.c_str());
	for(list<string>::const_iterator it = res.begin(); it != res.end(); it++) {
		if(it->compare(crinklerCanonicalName) != 0) {
			printf("Launching default linker at '%s'\n\n", it->c_str());
			char args[MAX_PATH];
			strcpy_s(args, GetCommandLine());

			STARTUPINFO siStartupInfo;
			PROCESS_INFORMATION piProcessInfo;
			memset(&siStartupInfo, 0, sizeof(siStartupInfo));
			memset(&piProcessInfo, 0, sizeof(piProcessInfo));
			siStartupInfo.cb = sizeof(siStartupInfo);

			if(!CreateProcess(it->c_str(),		//LPCSTR lpApplicationName
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
				Log::error(0, "", "failed to launch default linker, errorcode: %X", GetLastError());
			}

			//Wait until application has terminated
			WaitForSingleObject(piProcessInfo.hProcess, INFINITE);

			//Close process and thread handles
			CloseHandle(piProcessInfo.hThread);
			CloseHandle(piProcessInfo.hProcess);
			return;
		}
	}

	//Linker not found
	Log::error(0, "", "could not find default linker in path");
}

//TODO: report an error, if no files were specified
int main(int argc, char* argv[]) {
	//TODO: remove - hack for testing purposes
/*
	argc = 3;
	char* argv[] = {
		argv2[0],
		"@test\\buildtest.txt",
		"/CRINKLER",
		"/COMPMODE:SLOW"
	};
*/

	//find canonical name of the crinkler executable
	char crinklerCanonicalName[1024];
	GetFullPathName(argv[0], sizeof(crinklerCanonicalName), crinklerCanonicalName, NULL);
	char crinklerFilename[1024];
	_splitpath_s(crinklerCanonicalName, NULL, 0, NULL, 0, crinklerFilename, sizeof(crinklerFilename), NULL, 0);
	

	//cmdline parameters
	CmdParamInt hashsizeArg("HASHSIZE", "number of megabytes for hashing", "size in mb", CMD_PARAM_SHOW_RESTRICTION,
							10, 1024, 50);
	CmdParamInt hashtriesArg("HASHTRIES", "number of hashing tries", "number of hashing tries", 0,
							0, 1000, 10);
	CmdParamInt hunktriesArg("HUNKTRIES", "number of hashing tries", "number of hunk reordering tries", 0,
							0, 10000, 1000);
	CmdParamString entryArg("ENTRY", "name of the entrypoint", "symbol", 0, "");
	CmdParamString outArg("OUT", "output filename", "filename", 0, "out.exe");
	CmdParamSwitch crinklerFlag("CRINKLER", "enables crinkler", 0);
	CmdParamSwitch safeImportArg("SAFEIMPORT", "emit an error if a dll is missing", 0);
	CmdParamSwitch showProgressArg("PROGRESSGUI", "shows a progressbar", 0);
	CmdParamEnum subsystemArg("SUBSYSTEM", "select subsystem", 0, SUBSYSTEM_CONSOLE, 
						"WINDOWS", SUBSYSTEM_WINDOWS, "CONSOLE", SUBSYSTEM_CONSOLE, NULL);
	CmdParamEnum priorityArg("PRIORITY", "select priority", 0, BELOW_NORMAL_PRIORITY_CLASS, 
						"IDLE", IDLE_PRIORITY_CLASS, "BELOWNORMAL", BELOW_NORMAL_PRIORITY_CLASS, "NORMAL", NORMAL_PRIORITY_CLASS, NULL);
	CmdParamEnum compmodeArg("COMPMODE", "compression mode", 0, COMPRESSION_FAST,
						"FAST", COMPRESSION_FAST, 
						"SLOW", COMPRESSION_SLOW, NULL);
	CmdParamFlags verboseArg("VERBOSE", "selects verbose modes", 0, 0, 
							"LABELS", VERBOSE_LABELS, "IMPORTS", VERBOSE_IMPORTS,
							"MODELS", VERBOSE_MODELS, "FUNCTIONS", VERBOSE_FUNCTIONS, NULL);
	CmdParamFlags transformArg("TRANSFORM", "select transformations", 0, 0, 
							"CALLS", TRANSFORM_CALLS, NULL);
	CmdParamMultiString libpathArg("LIBPATH", "adds a path to the library search path", "dirs", CMD_PARAM_IS_SWITCH, 0);
	CmdParamMultiString rangeImportArg("RANGE", "use range importing for this dll", "dllname", CMD_PARAM_IS_SWITCH, 0);
	CmdParamMultiString replaceDllArg("REPLACEDLL", "replace a dll with another", "oldDLL=newDLL", CMD_PARAM_IS_SWITCH, 0);
	CmdParamMultiString filesArg("FILES", "list of filenames", "", CMD_PARAM_HIDE_IN_PARAM_LIST, 0);
	CmdLineInterface cmdline(CRINKLER_TITLE, CMDI_PARSE_FILES);

	cmdline.addParams(&crinklerFlag, &hashsizeArg, &hashtriesArg, &hunktriesArg, &entryArg, &outArg, &safeImportArg,
						&subsystemArg, &compmodeArg, &verboseArg, &transformArg, &libpathArg, 
						&rangeImportArg, &replaceDllArg, &filesArg, &priorityArg, &showProgressArg, NULL);
	cmdline.setCmdParameters(argc, argv);

	//print syntax?
	if(argc == 1) {
		cmdline.printSyntax();
		return 0;
	}

	cmdline.printHeader();
	cmdline.parse();

	//Run default linker or crinkler?
	if(!cmdline.removeToken("/CRINKLER") && toUpper(crinklerFilename).compare("CRINKLER") != 0) {
		runOriginalLinker(crinklerCanonicalName);
		return 0;
	}

	//set priority
	SetPriorityClass(GetCurrentProcess(), priorityArg.getValue());

	Crinkler crinkler;

	//set crinkler options
	crinkler.setImportingType(safeImportArg.getValue());
	crinkler.setEntry(entryArg.getValue());
	crinkler.setHashsize(hashsizeArg.getValue());
	crinkler.setSubsystem((SubsystemType)subsystemArg.getValue());
	crinkler.setCompressionType((CompressionType)compmodeArg.getValue());
	crinkler.setHashtries(hashtriesArg.getValue());
	crinkler.setVerboseFlags(verboseArg.getValue());
	crinkler.showProgressBar(showProgressArg.getValue());
	{
		list<string> lst = rangeImportArg.getList();
		crinkler.addRangeDlls(lst);
	}

	//print some info
	printf("Target: %s\n", outArg.getValue());
	printf("Subsystem type: %s\n", subsystemArg.getValue() == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
	printf("Compression mode: %s\n", compmodeArg.getValue() == COMPRESSION_SLOW ? "SLOW" : "FAST");
	printf("Hash size: %d MB\n", hashsizeArg.getValue());
	printf("Hash tries: %d\n", hashtriesArg.getValue());
	printf("Hunk tries: %d\n", hunktriesArg.getValue());
	printf("\n");

	//build search library+object search path
	string lib = ";.;";
	const char* path;
	while(path = libpathArg.getValue()) {
		lib += path;
		lib += ";";
	}
	lib += ";" + getEnv("LIB");
	lib += ";" + getEnv("PATH");

	//load files
	{
		printf("loading: \n");
		int c = 0;
		const char* filename;
		while(filename = filesArg.getValue()) {
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