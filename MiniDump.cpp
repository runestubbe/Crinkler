#include "MiniDump.h"

#include <windows.h>
#include <Dbghelp.h>
#include <cstdio>

LONG WINAPI ExitWithDump(struct _EXCEPTION_POINTERS* exceptionInfo)
{
	int dump_index = 0;

	HANDLE file = NULL;
	do {
		char buff[128];
		sprintf(buff, "dump%04d.dmp", dump_index);
		dump_index++;

		file = CreateFile(buff, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} while(file == NULL || file == INVALID_HANDLE_VALUE);

	MINIDUMP_EXCEPTION_INFORMATION mdei; 
	mdei.ThreadId           = GetCurrentThreadId(); 
	mdei.ExceptionPointers  = exceptionInfo; 
	mdei.ClientPointers     = FALSE;
	MINIDUMP_TYPE mdt       = MiniDumpNormal;

	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, mdt, (exceptionInfo != 0) ? &mdei : 0, 0, 0); 
	CloseHandle(file);

	return EXCEPTION_CONTINUE_SEARCH;
}

void EnableMiniDumps() {
	SetUnhandledExceptionFilter(ExitWithDump);
}