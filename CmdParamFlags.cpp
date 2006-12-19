#include "CmdParamFlags.h"
#include <cstdarg>
#include <Windows.h>
#include <iostream>
#include "StringMisc.h"

using namespace std;

CmdParamFlags::CmdParamFlags(const char* paramName, const char* description, int flags, int defaultValue, 
						   const char* flagName, int flagValue, ...) : 
CmdParam(paramName, description, NULL, flags | CMD_PARAM_IS_SWITCH | CMD_PARAM_TAKES_ARGUMENT | CMD_PARAM_ALLOW_MULTIPLE_DEFINITIONS){
	va_list ap;
	va_start(ap, flagValue);
	m_value = defaultValue;

	m_argumentDesc = "{";

	while(true) {
		m_argumentDesc += string(flagName) + "|";
		pair<string, int> p1(toUpper(flagName), flagValue);
		m_flagMap.insert(p1);

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
	map<string, int>::iterator it = m_flagMap.find(toUpper(str));
	if(it == m_flagMap.end()) {
		sprintf_s(errorMsg, buffsize, "unknown argument %s", str);
		return CMD_PARAM_INVALID;
	}

	m_value |= it->second;

	return CMD_PARAM_PARSE_OK;
}

int CmdParamFlags::getValue() {
	return m_value;
}