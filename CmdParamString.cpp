#include "CmdParamString.h"

CmdParamString::CmdParamString(const char* paramName, const char* description, const char* argumentDesciption, int flags, const char* defaultValue) :
						CmdParam(paramName, description, argumentDesciption, flags | CMD_PARAM_IS_SWITCH | CMD_PARAM_TAKES_ARGUMENT) {
	m_string = defaultValue;
}

int CmdParamString::parse(const char* str, char* errorMsg, int buffsize) {
	m_string = str;
	return CMD_PARAM_PARSE_OK;
}

const char* CmdParamString::getValue() {
	return m_string.c_str();
}