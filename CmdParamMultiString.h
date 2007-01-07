#pragma once
#ifndef _CMD_PARAM_MULTI_STRING_H_
#define _CMD_PARAM_MULTI_STRING_H_

#include "CmdParam.h"
#include <list>

class CmdParamMultiString : public CmdParam {
	std::list<std::string> m_strings;
	std::list<std::string>::const_iterator m_it;
public:
	CmdParamMultiString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue);

	int parse(const char* str, char* errorMsg, int buffsize);

	void reset();
	const char* getValue();
	std::list<std::string> getList();
};

#endif