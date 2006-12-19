#include "NameMangling.h"

using namespace std;

string removePrefix(const char* str) {
	string s = str;
	if(strlen(str) > 0 && (str[0] == '?' || str[0] == '@' || str[0] == '_'))
		s.erase(0, 1);

	return s;
}

string undecorate(const char* str) {
	string s = removePrefix(str);

	if(s.find_first_of('@') != s.npos)	//remove everything from '@'
		s.erase(s.find_first_of('@'));
	return s;
}
