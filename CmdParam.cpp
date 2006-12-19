#include "CmdParam.h"

using namespace std;

CmdParam::CmdParam(const char* parameterName, const char* description, const char* argumentDescription, int flags) {
	m_parameterName = parameterName;
	m_description = description;
	if(argumentDescription)
		m_argumentDesc = argumentDescription;
	m_flags = flags;
	m_numMatches = 0;
}

string CmdParam::toString() const {
	char buff[1024];
	char param[1024];
	if(m_flags & CMD_PARAM_TAKES_ARGUMENT) {
		sprintf_s(param, sizeof(param), "%s:%s", m_parameterName.c_str(), m_argumentDesc.c_str());
	} else {
		sprintf_s(param, sizeof(param), "%s", m_parameterName.c_str());
	}
	sprintf_s(buff, sizeof(buff), "/%-24s%s", param, ""/*m_description.c_str()*/);
	return string(buff);
}

const char* CmdParam::getParameterName() const {
	return m_parameterName.c_str();
}

const char* CmdParam::getDescription() const {
	return m_description.c_str();
}

int CmdParam::getFlags() const {
	return m_flags;
}

int CmdParam::getNumMatches() const {
	return m_numMatches;
}

const char* CmdParam::getArgumentDescription() const {
	return m_argumentDesc.c_str();
}