#pragma once
#ifndef _CMD_PARAM_INT_H_
#define _CMD_PARAM_INT_H_

#include "CmdParam.h"

class CmdParamInt : public CmdParam {
	int			m_minValue;
	int			m_maxValue;
	int			m_defaultValue;
	int			m_value;
public:
	CmdParamInt(const char* parameterName, const char* description, const char* argumentDescription, int flags,
				int minimumValue, int maximumValue, int defaultValue);

	int getValue() const;
	void setDefault(int v) { m_defaultValue = v; m_value = v; }

	int parse(const char* str, char* errorMsg, int buffsize);
	std::string toString() const;
	bool valid();
};

#endif
