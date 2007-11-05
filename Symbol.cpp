#include "Symbol.h"
#include <windows.h>
#include <dbghelp.h>

using namespace std;

Symbol::Symbol(const char* name, int value, unsigned int flags, Hunk* hunk, const char* miscString) :
	name(name), value(value), flags(flags), hunk(hunk)
{
	if(miscString)
		this->miscString = miscString;
}

std::string Symbol::getUndecoratedName() {
	string str = name;
	char buff[1024];
	UnDecorateSymbolName(str.c_str(), buff, sizeof(buff), UNDNAME_COMPLETE | UNDNAME_32_BIT_DECODE);
	return string(buff);
}