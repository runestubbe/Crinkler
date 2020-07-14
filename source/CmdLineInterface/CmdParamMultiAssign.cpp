#include "CmdParamMultiAssign.h"
#include <string>

using namespace std;

CmdParamMultiAssign::CmdParamMultiAssign(const char* paramName, const char* description, const char* argumentDesciption, int flags) :
	CmdParam(paramName, description, argumentDesciption, flags | PARAM_TAKES_ARGUMENT)
{
	m_it = m_strings.begin();
}

int CmdParamMultiAssign::Parse(const char* str, char* errorMsg, int buffsize) {
	string s = str;
	string::size_type pos = s.find('=');
	if(pos == string::npos) {
		if (GetFlags() & PARAM_ALLOW_MISSING_VALUE) {
			if (s.size() == 0) {
				sprintf_s(errorMsg, buffsize, "identifier must be non empty");
				return PARSE_INVALID;
			}
			m_strings.push_back(make_pair(s, ""));
			m_it = m_strings.begin();
			return PARSE_OK;
		}
		sprintf_s(errorMsg, buffsize, "argument must be an assignment");
		return PARSE_INVALID;
	}

	if(pos != s.rfind('=')) {
		sprintf_s(errorMsg, buffsize, "argument must contain exactly one assignment");
		return PARSE_INVALID;
	}

	string s1 = s.substr(0, pos);
	string s2 = s.substr(pos+1);
	if(s1.size() == 0 || s2.size() == 0) {
		sprintf_s(errorMsg, buffsize, "identifier must be non empty");
		return PARSE_INVALID;
	}

	m_strings.push_back(make_pair(s1,s2));
	m_it = m_strings.begin();
	return PARSE_OK;
}

void CmdParamMultiAssign::Next() {
	m_it++;
}

bool CmdParamMultiAssign::HasNext() const {
	return m_it != m_strings.end();
}

const char* CmdParamMultiAssign::GetValue1() {
	return HasNext() ? m_it->first.c_str() : NULL;
}
const char* CmdParamMultiAssign::GetValue2() {
	return HasNext() ? m_it->second.c_str() : NULL;
}