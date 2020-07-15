#include "NameMangling.h"

using namespace std;

string StripSymbolPrefix(const char* str) {
	string s = str;
	if(strlen(str) > 0 && (str[0] == '?' || str[0] == '@' || str[0] == '_'))
		s.erase(0, 1);

	return s;
}

string StripCrinklerSymbolPrefix(const char* str) {
	string s = str;
	string::size_type startpos = s.find("!");
	if(startpos != string::npos)
		s.erase(0, startpos+1);

	return s;
}

string UndecorateSymbolName(const char* str) {
	string s = StripSymbolPrefix(StripCrinklerSymbolPrefix(str).c_str());

	if(s.find_first_of('@') != s.npos)	//remove everything from '@'
		s.erase(s.find_first_of('@'));
	return s;
}
