#include "CmdParamSwitch.h"

CmdParamSwitch::CmdParamSwitch(const char* paramName, const char* description, int flags) : CmdParam(paramName, description, NULL, flags | PARAM_IS_SWITCH){
	m_value = false;
}

int CmdParamSwitch::Parse(const char* str, char* errorMsg, int buffsize) {
	m_value = true;
	return PARSE_OK;
}

bool CmdParamSwitch::GetValue() const {
	return m_value;
}
