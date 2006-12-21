#include "CmdParamMultiAssign.h"
#include <string>

using namespace std;

CmdParamMultiAssign::CmdParamMultiAssign(const char* paramName, const char* description, const char* argumentDesciption, int flags) :
	CmdParam(paramName, description, argumentDesciption, flags | CMD_PARAM_TAKES_ARGUMENT | CMD_PARAM_ALLOW_MULTIPLE_DEFINITIONS)
{
	m_it = m_strings.begin();
}

int CmdParamMultiAssign::parse(const char* str, char* errorMsg, int buffsize) {
	string s = str;
	string::size_type pos = s.find('=');
	if(pos == string::npos) {
		sprintf_s(errorMsg, buffsize, "argument must be an assignment");
		return CMD_PARAM_INVALID;
	}

	if(pos != s.rfind('=')) {
		sprintf_s(errorMsg, buffsize, "argument must contain exactly one assignment");
		return CMD_PARAM_INVALID;
	}

	string s1 = s.substr(0, pos);
	string s2 = s.substr(pos+1);
	if(s1.size() == 0 || s2.size() == 0) {
		sprintf_s(errorMsg, buffsize, "identifier must be non empty");
		return CMD_PARAM_INVALID;
	}

	StringPair sp(s1, s2);
	m_strings.push_back(sp);
	m_it = m_strings.begin();
	return CMD_PARAM_PARSE_OK;
}

void CmdParamMultiAssign::reset() {
	m_it = m_strings.begin();
}

StringPair* CmdParamMultiAssign::getValue() {
	if(m_it != m_strings.end())
		return &*(m_it++);
	else
		return NULL;
}

list<StringPair> CmdParamMultiAssign::getList() {
	return m_strings;
}