#pragma once
#ifndef _LOG_H_
#define _LOG_H_

class Log
{
public:
	static void Warning(const char* from, const char* msg, ...);
	static void Error(const char* from, const char* msg, ...);
	static void NonfatalError(const char* from, const char* msg, ...);
};

#endif
