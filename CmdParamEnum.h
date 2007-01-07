#pragma once
#ifndef _CMD_PARAM_ENUM_H_
#define _CMD_PARAM_ENUM_H_

#include "CmdParam.h"
#include <map>
#include <string>

class CmdParamEnum : public CmdParam {
	std::map<std::string, int>		m_enumMap;
	int								m_value;
public:
	CmdParamEnum(const char* paramName, const char* description, int flags, int defaultValue, const char* enumName, int enumValue, ...);

	int parse(const char* str, char* errorMsg, int buffsize);
	int getValue();
};

#endif