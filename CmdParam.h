#pragma once
#ifndef _CMD_PARAM_H_
#define _CMD_PARAM_H_

#include <string>

//TODO: handle multiple definitions of the same param

#define CMD_PARAM_SHOW_RESTRICTION				0x01
#define CMD_PARAM_IS_SWITCH						0x02
#define CMD_PARAM_TAKES_ARGUMENT				0x04
#define CMD_PARAM_ALLOW_MULTIPLE_DEFINITIONS	0x08
#define CMD_PARAM_HIDE_IN_PARAM_LIST			0x10

#define CMD_PARAM_PARSE_OK						0x00
#define CMD_PARAM_NO_MATCH						0x01
#define CMD_PARAM_INVALID						0x02

class CmdParam {
	std::string m_parameterName;
	std::string m_description;
	int			m_flags;
	int			m_numMatches;
	friend class CmdLineInterface;

protected:
	std::string m_argumentDesc;
public:
	CmdParam(const char* argumentName, const char* description, const char* argumentDescription, int flags);

	virtual int parse(const char* str, char* errorMsg, int buffsize) = 0;
	virtual std::string toString() const;

	const char* getArgumentDescription() const;
	const char* getParameterName() const;
	const char* getDescription() const;
	int getFlags() const;

	int getNumMatches() const;
};

#endif