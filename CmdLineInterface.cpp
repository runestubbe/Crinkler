#include "CmdLineInterface.h"
#include <iostream>
#include <fstream>
#include <windows.h>
#include <algorithm>
#include <cstdarg>

#include "MemoryFile.h"
#include "Log.h"
#include "StringMisc.h"

using namespace std;

CmdLineInterface::CmdLineInterface(const char* title, int flags) {
	m_title = title;
	m_flags = flags;
}

CmdLineInterface::~CmdLineInterface() {
}

//TODO: parse "" correctly
//TODO: is this tight?
void CmdLineInterface::addTokens(const char* str) {
	int len = strlen(str)+1;
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
			if(mf.getPtr() == NULL) {
				m_tokens.clear();
				Log::error(0, "", "failed to open file '%s'", arg);	//TODO: is this a LNK error?
				return false;
			}

			//add tokens from file
			//TODO: test unicode support
			if(mf.getSize() >= 2 && (*(unsigned short*) mf.getPtr()) == 0xFEFF) {	//UNICODE
				char* tmp = new char[mf.getSize()+2];
				printf("ret: %d\n", WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)(mf.getPtr()+2), -1, tmp, mf.getSize()+2, NULL, NULL));
				printf("unicode magic: %s\n", tmp);
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

			string argument = token;
			if(param->getFlags() & CMD_PARAM_IS_SWITCH) {
				if(token[0] == '/') {
					string::size_type p = token.find(":", 0);
					argument = "";
					bool foundArgument = false;

					if(p == string::npos) {
						p = token.size();
					} else {
						foundArgument = true;
						argument = token.substr(p+1, token.size()-p-1);
					}
					string swi = token.substr(1, p-1);
					if(toUpper(swi).compare(toUpper(param->getParameterName())) != 0) {	//case insensitive argument matching
						continue;	//wrong parameter
					}

					//check for the correct number of arguments (0 or 1)
					if((param->getFlags() & CMD_PARAM_TAKES_ARGUMENT) && !foundArgument) {
						cerr << "error parsing token: '" << token << "'\n  " << "parameter needs argument" << endl;
						return false;
					}
					if(!(param->getFlags() & CMD_PARAM_TAKES_ARGUMENT) && foundArgument) {
						cerr << "error parsing token: '" << token << "'\n  " << "parameter doesn't take any arguments" << endl;
						return false;
					}
				} else {
					continue;
				}
			} else if(token[0] == '/'){
				continue;
			}
			
			//parse argument and handle potential errors
			int hr = param->parse(argument.c_str(), errorMsg, sizeof(errorMsg));
			if(hr == CMD_PARAM_PARSE_OK) {
				if(param->m_numMatches && !(param->getFlags() & CMD_PARAM_ALLOW_MULTIPLE_DEFINITIONS)) {
					Log::warning(0, "", "parameter cannot be defined more than once '%s'", token.c_str());
				}
				param->m_numMatches++;
				nMatches++;
			} else if(hr == CMD_PARAM_INVALID) {
				Log::error(0, "", "cannot parse token '%s': %s", token.c_str(), errorMsg);
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
	} while(param = va_arg(ap, CmdParam*));

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
		if(! ((*it)->getFlags() & CMD_PARAM_HIDE_IN_PARAM_LIST))
			printf("      %s\n", (*it)->toString().c_str());
	}
}

void CmdLineInterface::printTokens() {
	printf("tokens: \n");
	for(list<string>::const_iterator it = m_tokens.begin(); it != m_tokens.end(); it++) {
		printf("%s ", it->c_str());
	}
	printf("\n");
}