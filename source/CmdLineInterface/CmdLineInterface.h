#pragma once
#ifndef _CMD_LINE_INTERFACE_H_
#define _CMD_LINE_INTERFACE_H_

#include "CmdParamInt.h"
#include "CmdParamSwitch.h"
#include "CmdParamString.h"
#include "CmdParamFlags.h"
#include "CmdParamMultiAssign.h"

#include <vector>
#include <string>

const int CMDI_PARSE_FILES = 0x01;

class CmdLineInterface {
	std::vector<CmdParam*>		m_params;
	std::vector<std::string>	m_tokens;
	std::string					m_title;
	int							m_flags;

	void AddTokens(const char* str);
public:
	CmdLineInterface(const char* title, int flags);
	~CmdLineInterface();

	void AddParams(CmdParam* param, ...);
	void PrintSyntax();
	void PrintHeader();

	bool SetCmdParameters(int argc, char* argv[]);
	bool RemoveToken(const char* str);
	bool Parse();
};

#endif
