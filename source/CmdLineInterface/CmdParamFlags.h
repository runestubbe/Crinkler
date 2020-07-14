#pragma once
#ifndef _CMD_PARAM_FLAGS_H_
#define _CMD_PARAM_FLAGS_H_

#include "CmdParam.h"
#include <map>
#include <string>

class CmdParamFlags : public CmdParam {
	std::map<std::string, int>		m_flagMap;
public:
	int								m_value;
	CmdParamFlags(const char* paramName, const char* description, int flags, int defaultValue, const char* flagName, int flagValue, ...);

	int		Parse(const char* str, char* errorMsg, int buffsize);

	int		GetValue();
	int		GetValueIfPresent(int fallback);
	void	SetDefault(int flag) { m_value = flag; }
};

#endif
