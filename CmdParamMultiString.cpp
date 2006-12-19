#include "CmdParamMultiString.h"

CmdParamMultiString::CmdParamMultiString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue) :
CmdParam(paramName, description, argumentDesciption, flags | CMD_PARAM_TAKES_ARGUMENT | CMD_PARAM_ALLOW_MULTIPLE_DEFINITIONS) {
	if(defaultValue != NULL) {
		m_strings.push_back(defaultValue);
	}
	m_it = m_strings.begin();
}

int CmdParamMultiString::parse(const char* str, char* errorMsg, int buffsize) {
	m_strings.push_back(str);
	m_it = m_strings.begin();
	return CMD_PARAM_PARSE_OK;
}

void CmdParamMultiString::reset() {
	m_it = m_strings.begin();
}

const char* CmdParamMultiString::getValue() {
	if(m_it != m_strings.end())
		return (m_it++)->c_str();
	else
		return NULL;
}

std::list<std::string> CmdParamMultiString::getList() {
	return m_strings;
}