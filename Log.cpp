#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include "Log.h"

void Log::warning(int code, const char* from, const char* msg, ...) {
	va_list args; 
	va_start(args, msg);
	char buff[16384];
	vsprintf_s(buff, sizeof(buff), msg, args);

	fprintf(stdout, "%s : warning: LNK%4d: %s\n", from, code, buff);
}

void Log::error(int code, const char* from, const char* msg, ...) {
	va_list args; 
	va_start(args, msg);
	char buff[16384];
	vsprintf_s(buff, sizeof(buff), msg, args);

	fprintf(stdout, "%s : fatal error: LNK%4d: %s\n", from, code, buff);
	fflush(stdout);
	ExitProcess(-1);
}
