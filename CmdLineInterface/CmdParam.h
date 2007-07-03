#pragma once
#ifndef _CMD_PARAM_H_
#define _CMD_PARAM_H_

#include <string>

#define PARAM_SHOW_CONSTRAINTS				0x01	//show parameter constraints in overview
#define PARAM_IS_SWITCH						0x02	//indicates that the parameters starts with a '/'
#define PARAM_TAKES_ARGUMENT				0x04	//parameter name should be followed by a ':'' and an argument ex. /VERBOSE:LABELS
#define PARAM_FORBID_MULTIPLE_DEFINITIONS	0x08	//forbids multiple definitions of this parameter
#define PARAM_HIDE_IN_PARAM_LIST			0x10	//hide parameter in parameter list

#define PARSE_OK							0x00	//the parameter parsed successfully
#define PARSE_NO_MATCH						0x01	//the input doesn't match this parameter
#define PARSE_INVALID						0x02	//the input matches this parameter, but is invalid

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
