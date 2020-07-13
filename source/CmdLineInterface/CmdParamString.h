#pragma once
#ifndef _CMD_PARAM_STRING_H_
#define _CMD_PARAM_STRING_H_

#include "CmdParam.h"
#include <vector>

class CmdParamString : public CmdParam {
	std::vector<std::string> m_strings;
	std::vector<std::string>::const_iterator m_it;
public:
	CmdParamString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue);

	int parse(const char* str, char* errorMsg, int buffsize);

	const char* getValue();
	void setDefault(const char* str) { m_strings.clear(); m_strings.push_back(str); m_it = m_strings.begin();}
	void next();
	bool hasNext() const;
	std::vector<std::string> getList();
};

#endif
