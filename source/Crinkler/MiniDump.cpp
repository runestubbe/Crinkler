#include "MiniDump.h"

#include <windows.h>
#include <Dbghelp.h>
#include <cstdio>

static CRITICAL_SECTION criticalSection;

LONG WINAPI ExitWithDump(struct _EXCEPTION_POINTERS* exceptionInfo)
{
	EnterCriticalSection(&criticalSection);

	int dump_index = 0;

	HANDLE file_mini = NULL;
	HANDLE file_full = NULL;
	char filename_mini[128];
	char filename_full[128];
	do {
		sprintf(filename_mini, "dump%04d_mini.dmp", dump_index);
		sprintf(filename_full, "dump%04d_full.dmp", dump_index);
		dump_index++;

		file_mini = CreateFile(filename_mini, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		file_full = CreateFile(filename_full, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	} while(file_mini == NULL || file_mini == INVALID_HANDLE_VALUE || file_full == NULL || file_full == INVALID_HANDLE_VALUE);

	MINIDUMP_EXCEPTION_INFORMATION mdei; 
	mdei.ThreadId           = GetCurrentThreadId(); 
	mdei.ExceptionPointers  = exceptionInfo; 
	mdei.ClientPointers     = FALSE;

	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file_mini, MiniDumpNormal, (exceptionInfo != 0) ? &mdei : 0, 0, 0); 
	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file_full, MiniDumpWithFullMemory, (exceptionInfo != 0) ? &mdei : 0, 0, 0); 
	CloseHandle(file_mini);
	CloseHandle(file_full);

	printf("\nOops! Crinkler has crashed.\nDump files written to %s and %s.\n", filename_mini, filename_full);
	fflush(stdout);
	ExitProcess(-1);
	return EXCEPTION_CONTINUE_SEARCH;
}

void EnableMiniDumps() {
	InitializeCriticalSection(&criticalSection);
	SetUnhandledExceptionFilter(ExitWithDump);
}