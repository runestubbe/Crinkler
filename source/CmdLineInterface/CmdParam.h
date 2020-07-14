#pragma once
#ifndef _CMD_PARAM_H_
#define _CMD_PARAM_H_

#include <string>

const int PARAM_SHOW_CONSTRAINTS =				0x01;	// Show parameter constraints in overview
const int PARAM_IS_SWITCH =						0x02;	// Indicate that the parameter starts with a '/'
const int PARAM_TAKES_ARGUMENT =				0x04;	// Parameter name should be followed by a ':' and an argument e.g. /VERBOSE:LABELS
const int PARAM_FORBID_MULTIPLE_DEFINITIONS =	0x08;	// Forbid multiple definitions of this parameter
const int PARAM_HIDE_IN_PARAM_LIST =			0x10;	// Hide parameter in parameter list
const int PARAM_ALLOW_NO_ARGUMENT_DEFAULT =		0x20;	// Allow omitting the value
const int PARAM_ALLOW_MISSING_VALUE =			0x40;   // Allow missing '=' and value in multi assign

const int PARSE_OK =							0x00;	// The parameter parsed successfully
const int PARSE_INVALID =						0x01;	// The input matches this parameter, but is invalid

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

	virtual int			Parse(const char* str, char* errorMsg, int buffsize) = 0;
	virtual std::string ToString() const;

	const char*			GetArgumentDescription() const;
	const char*			GetParameterName() const;
	const char*			GetDescription() const;
	int					GetFlags() const;

	int					GetNumMatches() const;
};

#endif
