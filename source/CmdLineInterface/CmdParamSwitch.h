#pragma once
#ifndef _CMD_PARAM_SWITCH_H_
#define _CMD_PARAM_SWITCH_H_

#include "CmdParam.h"

class CmdParamSwitch : public CmdParam {
	bool		m_value;
public:
	CmdParamSwitch(const char* paramName, const char* description, int flags);
	
	int		Parse(const char* str, char* errorMsg, int buffsize);
	bool	GetValue() const;
};

#endif
