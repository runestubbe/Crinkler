#include "CmdParamInt.h"
#include <sstream>

using namespace std;

CmdParamInt::CmdParamInt(const char* parameterName, const char* description, const char* argumentDescription, int flags,
					 int minimumValue, int maximumValue, int defaultValue) : CmdParam(parameterName, description, argumentDescription, flags | PARAM_IS_SWITCH | PARAM_TAKES_ARGUMENT | PARAM_FORBID_MULTIPLE_DEFINITIONS){
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
	if(len == 1) {
		m_value = v;
		if(v < m_minValue || v > m_maxValue) {
			sprintf_s(errorMsg, buffsize, "argument is not within the legal range [%d..%d]", m_minValue, m_maxValue);
			return PARSE_INVALID;
		}

		return PARSE_OK;
	}
	sprintf_s(errorMsg, buffsize, "expected integer argument");
	return PARSE_INVALID;
}

int CmdParamInt::getValue() const {
	return m_value;
}

bool CmdParamInt::valid() {
	return true;
}