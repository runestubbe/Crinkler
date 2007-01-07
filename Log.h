#pragma once
#ifndef _LOG_H_
#define _LOG_H_

class Log
{
public:
	static void warning(int code, const char* from, const char* msg, ...);
	static void error(int code, const char* from, const char* msg, ...);
};

#endif