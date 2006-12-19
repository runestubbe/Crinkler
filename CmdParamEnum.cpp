#include "CmdParamEnum.h"
#include <cstdarg>
#include <Windows.h>
#include "StringMisc.h"

using namespace std;

CmdParamEnum::CmdParamEnum(const char* paramName, const char* description, int flags, int defaultValue, 
						   const char* enumName, int enumValue, ...) : 
								CmdParam(paramName, description, NULL, flags | CMD_PARAM_IS_SWITCH | CMD_PARAM_TAKES_ARGUMENT){	
	va_list ap;
	va_start(ap, enumValue);
	m_value = defaultValue;

	m_argumentDesc = "{";
	while(true) {
		m_argumentDesc += string(enumName) + "|";
		pair<string, int> p1(toUpper(enumName), enumValue);
		m_enumMap.insert(p1);

		enumName = va_arg(ap, const char*);
		if(enumName == NULL) {
			m_argumentDesc[m_argumentDesc.size()-1] = '}';
			va_end(ap);
			return;
		}

		enumValue = va_arg(ap, int);
	};
}

int CmdParamEnum::parse(const char* str, char* errorMsg, int buffsize) {
	map<string, int>::iterator it = m_enumMap.find(toUpper(str));
	if(it == m_enumMap.end()) {
		sprintf_s(errorMsg, buffsize, "unknown argument %s", str);
		return CMD_PARAM_INVALID;
	}

	m_value = it->second;

	return CMD_PARAM_PARSE_OK;
}

int CmdParamEnum::getValue() {
	return m_value;
}