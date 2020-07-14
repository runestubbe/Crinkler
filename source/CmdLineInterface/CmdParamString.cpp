#include "CmdParamString.h"

CmdParamString::CmdParamString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue) :
CmdParam(paramName, description, argumentDesciption, flags | PARAM_TAKES_ARGUMENT) {
	if(defaultValue != NULL) {
		m_strings.push_back(defaultValue);
	}
	m_it = m_strings.begin();
}

int CmdParamString::Parse(const char* str, char* errorMsg, int buffsize) {
	if(GetFlags() & PARAM_FORBID_MULTIPLE_DEFINITIONS)
		m_strings.clear();
	m_strings.push_back(str);
	m_it = m_strings.begin();
	return PARSE_OK;
}

const char* CmdParamString::GetValue() {
	return HasNext() ? m_it->c_str() : NULL;
}

void CmdParamString::Next() {
	m_it++;
}

bool CmdParamString::HasNext() const {
	return m_it != m_strings.end();
}

std::vector<std::string> CmdParamString::GetList() {
	return m_strings;
}
