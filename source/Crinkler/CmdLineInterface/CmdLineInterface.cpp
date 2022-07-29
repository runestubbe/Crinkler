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

void CmdLineInterface::AddTokens(const char* str) {
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

bool CmdLineInterface::RemoveToken(const char* str) {
	for(auto it = m_tokens.begin(); it != m_tokens.end(); it++) {
		if(ToUpper(*it).compare(str) == 0) {
			m_tokens.erase(it);
			return true;
		}
	}
	return false;
}

bool CmdLineInterface::SetCmdParameters(int argc, char* argv[]) {
	m_tokens.clear();

	while(--argc) {
		const char* arg = *(++argv);
		if(m_flags & CMDI_PARSE_FILES && arg[0] == '@') {	// Expand command files (@file)
			arg++;	// Skip leading '@'
			MemoryFile mf(arg);

			// Add tokens from file
			if(mf.GetSize() >= 2 && (*(unsigned short*) mf.GetPtr()) == 0xFEFF) {	// UNICODE
				char* tmp = new char[mf.GetSize()+2];
				WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)(mf.GetPtr()+2), -1, tmp, mf.GetSize()+2, NULL, NULL);
				AddTokens(tmp);
				delete[] tmp;
			} else {	// ASCII
				AddTokens(mf.GetPtr());
			}
		} else {
			// Command line parser removes all "'s
			char token[1024];
			sprintf_s(token, sizeof(token), "\"%s\"", arg);
			AddTokens(token);
		}
	}
	return true;
}

bool CmdLineInterface::Parse() {
	for (string token : m_tokens) {
		char errorMsg[1024];

		int nMatches = 0;
		for(CmdParam* param : m_params) {
			bool hasArgument = false;
			string argument = token;
			if(param->GetFlags() & PARAM_IS_SWITCH) {
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
					if(ToUpper(swi).compare(ToUpper(param->GetParameterName())) != 0) {	// Case-insensitive argument matching
						continue;	// Wrong parameter
					}

					if(hasArgument && argument.size() == 0) {
						printf("Error: argument must be non-empty\n");
						return false;
					}

					// Check for the correct number of arguments (0 or 1)
					if((param->GetFlags() & PARAM_TAKES_ARGUMENT) &&
						!(param->GetFlags() & PARAM_ALLOW_NO_ARGUMENT_DEFAULT) && !hasArgument) {
						printf("Error: error parsing token: '%s'\n  parameter needs argument\n", token.c_str());
						return false;
					}
					if(!(param->GetFlags() & PARAM_TAKES_ARGUMENT) && hasArgument) {
						printf("Error: error parsing token: '%s'\n  parameter doesn't take any arguments\n", token.c_str());
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
			if(!(param->GetFlags() & PARAM_ALLOW_NO_ARGUMENT_DEFAULT) || hasArgument) {
				hr = param->Parse(argument.c_str(), errorMsg, sizeof(errorMsg));	// Skip parsing of argument, if it is allowed
			}
			if(hr == PARSE_OK) {
				if(param->m_numMatches && (param->GetFlags() & PARAM_FORBID_MULTIPLE_DEFINITIONS)) {
					printf("Error: parameter cannot be defined more than once '%s'\n", token.c_str());
					return false;
				}
				param->m_numMatches++;
				nMatches++;
			} else if(hr == PARSE_INVALID) {
				printf("Error: cannot parse token '%s': %s\n", token.c_str(), errorMsg);
				return false;
			}
		}

		if(nMatches < 1) {
			printf("Ignoring unknown argument '%s'\n", token.c_str());
		}
	}

	return true;
}

void CmdLineInterface::AddParams(CmdParam* param, ...) {
	va_list ap;
	va_start(ap, param);

	do {
		m_params.push_back(param);
	} while((param = va_arg(ap, CmdParam*)) != NULL);

	va_end(ap);
}

void CmdLineInterface::PrintHeader() {
	printf("%s\n\n", m_title.c_str());
}

void CmdLineInterface::PrintSyntax() {
	PrintHeader();
	printf("   options:\n\n");

	for(CmdParam* param : m_params) {
		if(! (param->GetFlags() & PARAM_HIDE_IN_PARAM_LIST))
			printf("      %s\n", param->ToString().c_str());
	}
}
