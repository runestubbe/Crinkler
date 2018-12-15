#include "CmdLineInterface.h"
#include <iostream>
#include <windows.h>

#include "../StringMisc.h"
#include "../MemoryFile.h"

using namespace std;

CmdLineInterface::CmdLineInterface(const char* title, int flags) {
	m_title = title;
	m_flags = flags;
}

CmdLineInterface::~CmdLineInterface() {
}

void CmdLineInterface::addTokens(const char* str) {
	int len = (int)strlen(str)+1;
	char* tmp = new char[len];
	char* ptr = tmp;
	char* token_start = tmp;
	bool inQuote = false;
	bool inWhitespace = true;
	for(int i = 0; i < len; i++) {
		switch(str[i]) {
			case '"':
				inQuote = !inQuote;
				break;
			case 0x0A:
			case 0x0D:
			case 0x09:
			case 0x20:
			case 0x00:
				if(!inQuote) {
					*ptr++ = 0;
					if(!inWhitespace) {
						m_tokens.push_back(token_start);
					}
					inWhitespace = true;
					token_start = ptr;
				} else {
					*ptr++ = str[i];
				}
				break;
			default:
				inWhitespace = false;
				*ptr++ = str[i];
				break;
		}
	}

	delete[] tmp;
}

bool CmdLineInterface::removeToken(const char* str) {
	for(list<string>::iterator it = m_tokens.begin(); it != m_tokens.end(); it++) {
		if(toUpper(*it).compare(str) == 0) {
			m_tokens.erase(it);
			return true;
		}
	}
	return false;
}

bool CmdLineInterface::setCmdParameters(int argc, char* argv[]) {
	m_tokens.clear();

	while(--argc) {
		const char* arg = *(++argv);
		if(m_flags & CMDI_PARSE_FILES && arg[0] == '@') {	//expand command files (@file)
			arg++;	//skip leading '@'
			MemoryFile mf(arg);

			//add tokens from file
			if(mf.getSize() >= 2 && (*(unsigned short*) mf.getPtr()) == 0xFEFF) {	//UNICODE
				char* tmp = new char[mf.getSize()+2];
				WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)(mf.getPtr()+2), -1, tmp, mf.getSize()+2, NULL, NULL);
				addTokens(tmp);
				delete[] tmp;
			} else {	//ASCII
				addTokens(mf.getPtr());
			}
		} else {
			//commandline parser removes all "'s
			char token[1024];
			sprintf_s(token, sizeof(token), "\"%s\"", arg);
			addTokens(token);
		}
	}
	return true;
}

bool CmdLineInterface::parse() {
	for(list<string>::iterator it = m_tokens.begin(); it != m_tokens.end(); it++) {
		string token = *it;
		char errorMsg[1024];

		int nMatches = 0;
		for(list<CmdParam*>::const_iterator jt = m_params.begin(); jt != m_params.end(); jt++) {
			CmdParam* param = *jt;

			bool hasArgument = false;
			string argument = token;
			if(param->getFlags() & PARAM_IS_SWITCH) {
				if(token[0] == '/') {
					string::size_type split_index = token.find(":", 0);
					argument = "";

					
					if(split_index == string::npos) {
						split_index = token.size();
					} else {
						hasArgument = true;
						argument = token.substr(split_index+1, token.size()-split_index-1);
					}
					string swi = token.substr(1, split_index-1);
					if(toUpper(swi).compare(toUpper(param->getParameterName())) != 0) {	//case insensitive argument matching
						continue;	//wrong parameter
					}

					if(hasArgument && argument.size() == 0) {
						cerr << "error: argument must be non-empty" << endl;
						return false;
					}

					//check for the correct number of arguments (0 or 1)
					if((param->getFlags() & PARAM_TAKES_ARGUMENT) &&
						!(param->getFlags() & PARAM_ALLOW_NO_ARGUMENT_DEFAULT) && !hasArgument) {
						cerr << "error: error parsing token: '" << token << "'\n  " << "parameter needs argument" << endl;
						return false;
					}
					if(!(param->getFlags() & PARAM_TAKES_ARGUMENT) && hasArgument) {
						cerr << "error: error parsing token: '" << token << "'\n  " << "parameter doesn't take any arguments" << endl;
						return false;
					}
				} else {
					continue;
				}
			} else if(token[0] == '/'){
				continue;
			}
			
			//parse argument and handle potential errors
			int hr = PARSE_OK;
			if(!(param->getFlags() & PARAM_ALLOW_NO_ARGUMENT_DEFAULT) || hasArgument) {
				hr = param->parse(argument.c_str(), errorMsg, sizeof(errorMsg));	//skip parsing of argument, if we are in the special default form
			}
			if(hr == PARSE_OK) {
				if(param->m_numMatches && (param->getFlags() & PARAM_FORBID_MULTIPLE_DEFINITIONS)) {
					cerr << "error: parameter cannot be defined more than once '" << token.c_str() << "'" << endl;
					return false;
				}
				param->m_numMatches++;
				nMatches++;
			} else if(hr == PARSE_INVALID) {
				cerr << "error: cannot parse token '" << token.c_str() << "': " << errorMsg << endl;
				return false;
			}
		}

		if(nMatches < 1) {
			cerr << "Ignoring unknown argument '" << token << "'" << endl;
		}
	}

	return true;
}

void CmdLineInterface::addParams(CmdParam* param, ...) {
	va_list ap;
	va_start(ap, param);

	do {
		m_params.push_back(param);
	} while((param = va_arg(ap, CmdParam*)) != NULL);

	va_end(ap);
}

void CmdLineInterface::printHeader() {
	//title
	printf("%s\n\n", m_title.c_str());
}

void CmdLineInterface::printSyntax() {
	printHeader();

	//options
	printf("   options:\n\n");

	for(list<CmdParam*>::const_iterator it = m_params.begin(); it != m_params.end(); it++) {
		if(! ((*it)->getFlags() & PARAM_HIDE_IN_PARAM_LIST))
			printf("      %s\n", (*it)->toString().c_str());
	}
}
