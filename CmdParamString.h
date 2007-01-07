#pragma once
#ifndef _CMD_PARAM_STRING_H_
#define _CMD_PARAM_STRING_H_

#include "CmdParam.h"

class CmdParamString : public CmdParam {
	std::string m_string;
public:
	CmdParamString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue);

	int parse(const char* str, char* errorMsg, int buffsize);

	const char* getValue();
};

#endif