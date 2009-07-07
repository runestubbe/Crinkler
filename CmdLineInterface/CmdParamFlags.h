#pragma once
#ifndef _CMD_PARAM_FLAGS_H_
#define _CMD_PARAM_FLAGS_H_

#include "CmdParam.h"
#include <map>
#include <string>

class CmdParamFlags : public CmdParam {
	std::map<std::string, int>		m_flagMap;
	int								m_value;
public:
	CmdParamFlags(const char* paramName, const char* description, int flags, int defaultValue, const char* flagName, int flagValue, ...);

	int parse(const char* str, char* errorMsg, int buffsize);

	int getValue();
	void setDefault(int flag) { m_value = flag; }
};

#endif
