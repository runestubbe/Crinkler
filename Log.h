#pragma once
#ifndef _LOG_H_
#define _LOG_H_

class Log
{
public:
	static void warning(const char* from, const char* msg, ...);
	static void error(const char* from, const char* msg, ...);
};

#endif
