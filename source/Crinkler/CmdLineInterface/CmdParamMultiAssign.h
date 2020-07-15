#pragma once
#ifndef _CMD_PARAM_MULTI_ASSIGN_H_
#define _CMD_PARAM_MULTI_ASSIGN_H_

#include "CmdParam.h"
#include <vector>

class CmdParamMultiAssign : public CmdParam {
	std::vector<std::pair<std::string, std::string> > m_strings;
	std::vector<std::pair<std::string, std::string> >::iterator m_it;
public:
	CmdParamMultiAssign(const char* paramName, const char* description, const char* argumentDesciption, int flags);

	int			Parse(const char* str, char* errorMsg, int buffsize);

	const char* GetValue1() const	{ return HasNext() ? m_it->first.c_str() : NULL; }
	const char* GetValue2() const	{ return HasNext() ? m_it->second.c_str() : NULL; }
	void		Next()				{ m_it++; }
	bool		HasNext() const		{ return m_it != m_strings.end(); }
};

#endif
