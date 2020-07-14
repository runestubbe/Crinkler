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

	const char* GetValue1();
	const char* GetValue2();
	void		Next();
	bool		HasNext() const;
};

#endif
