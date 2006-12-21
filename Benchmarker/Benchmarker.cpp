#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <ctime>

using namespace std;

const char* CRINKLER_OPTIONS = "/CRINKLER "
"/LIBPATH:\"C:\\Program Files\\Microsoft Visual Studio .NET 2003\\VC7\\LIB;"
"C:\\Program Files\\Microsoft Visual Studio .NET 2003\\VC7\\PlatformSDK\\LIB\"";

BOOL listFiles(const char* path, const char* extension, vector<string>& files) {
	WIN32_FIND_DATA findData;
	string searchString = string(path) + string("/") + extension;
	HANDLE fileHandle = FindFirstFile(searchString.c_str(), &findData);
	if(fileHandle == INVALID_HANDLE_VALUE)
		return FALSE;

	BOOL notDone;
	do {
		string filename = string(path) + "/";
		filename += findData.cFileName;
		files.push_back(filename);
		notDone = FindNextFile(fileHandle, &findData);
	} while(notDone);

	FindClose(fileHandle);
	return TRUE;
}

void deleteOldFiles() {
	vector<string> files;
	listFiles("test", "*.exe", files);
	for(vector<string>::const_iterator it = files.begin(); it != files.end(); it++) {
		
		if(DeleteFile(it->c_str()))
			printf("deleting file: %s\n", it->c_str());
		else
			printf("failed to delete file: %s\n", it->c_str());
	}
}

void cls()
{
	COORD coordScreen = { 0, 0 }; /* here's where we'll home the cursor */
	DWORD cCharsWritten;
	CONSOLE_SCREEN_BUFFER_INFO csbi; /* to get buffer info */
	DWORD dwConSize; /* number of character cells in the current buffer */

	/* get the output console handle */
	HANDLE hConsole=GetStdHandle(STD_OUTPUT_HANDLE);
	/* get the number of character cells in the current buffer */
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
	/* fill the entire screen with blanks */
	FillConsoleOutputCharacter(hConsole, (TCHAR) ' ',
		dwConSize, coordScreen, &cCharsWritten);
	/* get the current text attribute */
	GetConsoleScreenBufferInfo(hConsole, &csbi);
	/* now set the buffer's attributes accordingly */
	FillConsoleOutputAttribute(hConsole, csbi.wAttributes,
		dwConSize, coordScreen, &cCharsWritten);
	/* put the cursor at (0, 0) */
	SetConsoleCursorPosition(hConsole, coordScreen);
	return;
}

int main(int argc, char* argv[]) {
	string rootPath = "test";
	deleteOldFiles();

	string extraOptions;
	for(int i = 1; i < argc; i++) {
		extraOptions += string(argv[i]) + " ";
	}
	
	int stime = clock();
	vector<string> responseFiles;
	listFiles(rootPath.c_str(), "build*.txt", responseFiles);
	vector<string> exeFiles;
	for(vector<string>::const_iterator it = responseFiles.begin(); it != responseFiles.end(); it++) {
		char cmdString[1024];
		sprintf_s(cmdString, "release\\crinkler.exe %s %s @%s", CRINKLER_OPTIONS, extraOptions.c_str(), it->c_str());

		system(cmdString);

		string exeName = rootPath + "/" + it->substr(rootPath.size()+6, it->size() - rootPath.size() - 10) + ".exe";
		exeFiles.push_back(exeName);
	}

	cls();
	printf("      name                size\n");
	int total = 0;
	for(vector<string>::const_iterator it = exeFiles.begin(); it != exeFiles.end(); it++) {
		printf("%20s: ", it->c_str());
		WIN32_FIND_DATA findData;
		HANDLE fileHandle = FindFirstFile(it->c_str(), &findData);
		if(fileHandle != INVALID_HANDLE_VALUE) {
			total += findData.nFileSizeLow;
			printf("% 5d bytes", findData.nFileSizeLow);
			FindClose(fileHandle);
		} else {
			printf("    N/A");
		}
		
		printf("\n");
	}
	printf("total:             %8d bytes\n\n", total);

	int secs = (clock() - stime) / CLOCKS_PER_SEC;
	printf("time spent %3dm%02ds\n", secs/60, secs%60);

	return 0;
}
