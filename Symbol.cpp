#include "Symbol.h"
#include <windows.h>
#include <dbghelp.h>

using namespace std;

Symbol::Symbol(const char* name, int value, unsigned int flags, Hunk* hunk) :
	name(name), value(value), flags(flags), hunk(hunk)
{

}

//TODO: pretty, really??
std::string Symbol::getPrettyName() {
	string str = name;
	if(str.size() > 3) {
		string::size_type pos = str.find("]", 0);
		if(str[0] == 'l' && str[1] == '[' && pos != string::npos) {
			str = name.substr(pos+1, name.size()-pos-1);
		}
	}

	return str;
}

std::string Symbol::getUndecoratedName() {
	string str = name;
	char buff[1024];
	UnDecorateSymbolName(str.c_str(), buff, sizeof(buff), UNDNAME_COMPLETE | UNDNAME_32_BIT_DECODE);
	return string(buff);
}