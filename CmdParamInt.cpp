#include "CmdParamInt.h"
#include <sstream>

using namespace std;

CmdParamInt::CmdParamInt(const char* parameterName, const char* description, const char* argumentDescription, int flags,
					 int minimumValue, int maximumValue, int defaultValue) : CmdParam(parameterName, description, argumentDescription, flags | CMD_PARAM_IS_SWITCH | CMD_PARAM_TAKES_ARGUMENT){
	 m_minValue = minimumValue;
	 m_maxValue = maximumValue;
	 m_defaultValue = defaultValue;
	 m_value = defaultValue;
}

std::string CmdParamInt::toString() const {
	return CmdParam::toString();
}

int CmdParamInt::parse(const char* str, char* errorMsg, int buffsize) {
	int v;
	char c;
	
	int len = sscanf_s(str, "%d%c", &v, &c);
	printf("len: %d\n", len);
	if(len == 1) {
		m_value = v;
		if(v < m_minValue || v > m_maxValue) {
			sprintf_s(errorMsg, buffsize, "argument is not within the legal range [%d..%d]", m_minValue, m_maxValue);
			return CMD_PARAM_INVALID;
		}

		return CMD_PARAM_PARSE_OK;
	}
	sprintf_s(errorMsg, buffsize, "expected integer argument");
	return CMD_PARAM_INVALID;
}

int CmdParamInt::getValue() const {
	return m_value;
}

bool CmdParamInt::valid() {
	return true;
}