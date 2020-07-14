#include "CmdLineInterface.h"
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
	for(auto it = m_tokens.begin(); it != m_tokens.end(); it++) {
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
		if(m_flags & CMDI_PARSE_FILES && arg[0] == '@') {	// Expand command files (@file)
			arg++;	// Skip leading '@'
			MemoryFile mf(arg);

			// Add tokens from file
			if(mf.getSize() >= 2 && (*(unsigned short*) mf.getPtr()) == 0xFEFF) {	// UNICODE
				char* tmp = new char[mf.getSize()+2];
				WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)(mf.getPtr()+2), -1, tmp, mf.getSize()+2, NULL, NULL);
				addTokens(tmp);
				delete[] tmp;
			} else {	// ASCII
				addTokens(mf.getPtr());
			}
		} else {
			// Command line parser removes all "'s
			char token[1024];
			sprintf_s(token, sizeof(token), "\"%s\"", arg);
			addTokens(token);
		}
	}
	return true;
}

bool CmdLineInterface::parse() {
	for (string token : m_tokens) {
		char errorMsg[1024];

		int nMatches = 0;
		for(CmdParam* param : m_params) {
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
					if(toUpper(swi).compare(toUpper(param->getParameterName())) != 0) {	// Case-insensitive argument matching
						continue;	// Wrong parameter
					}

					if(hasArgument && argument.size() == 0) {
						fprintf(stderr, "error: argument must be non-empty\n");
						return false;
					}

					// Check for the correct number of arguments (0 or 1)
					if((param->getFlags() & PARAM_TAKES_ARGUMENT) &&
						!(param->getFlags() & PARAM_ALLOW_NO_ARGUMENT_DEFAULT) && !hasArgument) {
						fprintf(stderr, "error: error parsing token: '%s'\n  parameter needs argument\n", token.c_str());
						return false;
					}
					if(!(param->getFlags() & PARAM_TAKES_ARGUMENT) && hasArgument) {
						fprintf(stderr, "error: error parsing token: '%s'\n  parameter doesn't take any arguments\n", token.c_str());
						return false;
					}
				} else {
					continue;
				}
			} else if(token[0] == '/'){
				continue;
			}
			
			// Parse argument and handle potential errors
			int hr = PARSE_OK;
			if(!(param->getFlags() & PARAM_ALLOW_NO_ARGUMENT_DEFAULT) || hasArgument) {
				hr = param->parse(argument.c_str(), errorMsg, sizeof(errorMsg));	// Skip parsing of argument, if it is allowed
			}
			if(hr == PARSE_OK) {
				if(param->m_numMatches && (param->getFlags() & PARAM_FORBID_MULTIPLE_DEFINITIONS)) {
					fprintf(stderr, "error: parameter cannot be defined more than once '%s'\n", token.c_str());
					return false;
				}
				param->m_numMatches++;
				nMatches++;
			} else if(hr == PARSE_INVALID) {
				fprintf(stderr, "error: cannot parse token '%s': %s\n", token.c_str(), errorMsg);
				return false;
			}
		}

		if(nMatches < 1) {
			fprintf(stderr, "Ignoring unknown argument '%s'\n", token.c_str());
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
	printf("%s\n\n", m_title.c_str());
}

void CmdLineInterface::printSyntax() {
	printHeader();
	printf("   options:\n\n");

	for(CmdParam* param : m_params) {
		if(! (param->getFlags() & PARAM_HIDE_IN_PARAM_LIST))
			printf("      %s\n", param->toString().c_str());
	}
}
