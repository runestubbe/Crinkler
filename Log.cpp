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

	printf("\n%s: warning LNK: %s\n", from, buff);
}

void Log::error(const char* from, const char* msg, ...) {
	va_list args; 
	va_start(args, msg);
	char buff[4096];
	vsprintf_s(buff, sizeof(buff), msg, args);

	printf("\n%s: error LNK: %s\n", from, buff);
	fflush(stdout);
	exit(-1);
}

void Log::nonfatalError(const char* from, const char* msg, ...) {
	va_list args; 
	va_start(args, msg);
	char buff[4096];
	vsprintf_s(buff, sizeof(buff), msg, args);

	printf("\n%s: error LNK: %s", from, buff);
}
