#pragma once
#ifndef _CMD_PARAM_STRING_H_
#define _CMD_PARAM_STRING_H_

#include "CmdParam.h"
#include <list>

class CmdParamString : public CmdParam {
	std::list<std::string> m_strings;
	std::list<std::string>::const_iterator m_it;
public:
	CmdParamString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue);

	int parse(const char* str, char* errorMsg, int buffsize);

	const char* getValue();
	void setDefault(const char* str) { m_strings.clear(); m_strings.push_back(str); m_it = m_strings.begin();}
	void next();
	bool hasNext() const;
	std::list<std::string> getList();
};

#endif
