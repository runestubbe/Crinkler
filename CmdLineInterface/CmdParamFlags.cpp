#include "CmdParamFlags.h"
#include <cstdarg>
#include "../StringMisc.h"

using namespace std;

CmdParamFlags::CmdParamFlags(const char* paramName, const char* description, int flags, int defaultValue, 
						   const char* flagName, int flagValue, ...) : 
CmdParam(paramName, description, NULL, flags | PARAM_IS_SWITCH | PARAM_TAKES_ARGUMENT){
	va_list ap;
	va_start(ap, flagValue);
	m_value = defaultValue;

	m_argumentDesc = "{";

	while(true) {
		m_argumentDesc += string(flagName) + "|";
		m_flagMap.insert(make_pair(toUpper(flagName), flagValue));

		flagName = va_arg(ap, const char*);
		if(flagName == NULL) {
			m_argumentDesc[m_argumentDesc.size()-1] = '}';
			va_end(ap);
			return;
		}

		flagValue = va_arg(ap, int);
	}
}

int CmdParamFlags::parse(const char* str, char* errorMsg, int buffsize) {
	std::string s = str;
	map<string, int>::iterator it = m_flagMap.find(toUpper(s.substr(0, s.find(','))));
	if(it == m_flagMap.end()) {
		sprintf_s(errorMsg, buffsize, "unknown argument %s", str);
		return PARSE_INVALID;
	}

	if(getFlags() & PARAM_FORBID_MULTIPLE_DEFINITIONS)
		m_value = it->second;
	else
		m_value |= it->second;

	return PARSE_OK;
}

int CmdParamFlags::getValue() {
	return m_value;
}

int CmdParamFlags::getValueIfPresent(int fallback) {
	return getNumMatches() > 0 ? getValue() : fallback;
}

