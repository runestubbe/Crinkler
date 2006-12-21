#ifndef _CMD_PARAM_MULTI_ASSIGN_H_
#define _CMD_PARAM_MULTI_ASSIGN_H_

#include "CmdParam.h"
#include "StringMisc.h"
#include <list>

class CmdParamMultiAssign : public CmdParam {
	std::list<StringPair> m_strings;
	std::list<StringPair>::iterator m_it;
public:
	CmdParamMultiAssign(const char* paramName, const char* description, const char* argumentDesciption, int flags);

	int parse(const char* str, char* errorMsg, int buffsize);

	void reset();
	StringPair* getValue();
	std::list<StringPair> getList();
};

#endif