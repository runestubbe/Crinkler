#pragma once
#ifndef _CMD_PARAM_H_
#define _CMD_PARAM_H_

#include <string>

const int PARAM_SHOW_CONSTRAINTS =				0x01;	//show parameter constraints in overview
const int PARAM_IS_SWITCH =						0x02;	//indicates that the parameters starts with a '/'
const int PARAM_TAKES_ARGUMENT =				0x04;	//parameter name should be followed by a ':'' and an argument ex. /VERBOSE:LABELS
const int PARAM_FORBID_MULTIPLE_DEFINITIONS =	0x08;	//forbids multiple definitions of this parameter
const int PARAM_HIDE_IN_PARAM_LIST =			0x10;	//hide parameter in parameter list
const int PARAM_ALLOW_NO_ARGUMENT_DEFAULT =		0x20;	//allows a form where /FOO eq. /FOO:default

const int PARSE_OK =							0x00;	//the parameter parsed successfully
const int PARSE_NO_MATCH =						0x01;	//the input doesn't match this parameter
const int PARSE_INVALID =						0x02;	//the input matches this parameter, but is invalid

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
	virtual ~CmdParam() {};

	virtual int parse(const char* str, char* errorMsg, int buffsize) = 0;
	virtual std::string toString() const;

	const char* getArgumentDescription() const;
	const char* getParameterName() const;
	const char* getDescription() const;
	int getFlags() const;

	int getNumMatches() const;
};

#endif
