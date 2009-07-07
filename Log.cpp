#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <windows.h>
#include "Log.h"

void Log::warning(const char* from, const char* msg, ...) {
	va_list args; 
	va_start(args, msg);
	char buff[4096];
	vsprintf_s(buff, sizeof(buff), msg, args);

	fprintf(stdout, "\n%s: warning: LNK: %s\n", from, buff);
}

void Log::error(const char* from, const char* msg, ...) {
	va_list args; 
	va_start(args, msg);
	char buff[4096];
	vsprintf_s(buff, sizeof(buff), msg, args);

	
	fprintf(stdout, "\n%s: error: LNK: %s\n", from, buff);
	fflush(stdout);
	Sleep(5000);
	exit(-1);
}
