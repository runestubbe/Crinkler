#ifndef _CMD_LINE_INTERFACE_H_
#define _CMD_LINE_INTERFACE_H_

#include "CmdParamInt.h"
#include "CmdParamSwitch.h"
#include "CmdParamString.h"
#include "CmdParamEnum.h"
#include "CmdParamFlags.h"
#include "CmdParamMultiString.h"

#include <list>
#include <string>

#define CMDI_PARSE_FILES		0x01

class CmdLineInterface {
	std::list<CmdParam*>	m_params;
	std::list<std::string>	m_tokens;
	std::string				m_title;
	int						m_flags;

	void addTokens(const char* str);
public:
	CmdLineInterface(const char* title, int flags);
	~CmdLineInterface();

	void addParams(CmdParam* param, ...);
	void printSyntax();
	void printHeader();

	void printTokens();	//TODO: remove, debug function

	bool setCmdParameters(int argc, char* argv[]);
	bool removeToken(const char* str);
	bool parse();
};

#endif