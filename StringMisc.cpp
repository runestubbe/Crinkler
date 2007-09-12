#include "StringMisc.h"
#include <algorithm>

using namespace std;

string toUpper(const string& s) {
	string d(s);
	transform(d.begin(), d.end(), d.begin(), (int(*)(int))toupper);
	return d;
}

string toLower(const string& s) {
	string d(s);
	transform(d.begin(), d.end(), d.begin(), (int(*)(int))tolower);
	return d;
}

std::string stripPath(const std::string& s) {
	int idx = s.size()-1;
	while(idx >= 0 && s[idx] != ':' && s[idx] != '/' && s[idx] != '\\') idx--;
	return s.substr(idx+1, s.size() - (idx+1));
}

string toHtml(char c) {
	char buff[16];
	if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') 
		|| (c == '_'))
		sprintf_s(buff, sizeof(buff), "%c", c);
	else
		sprintf_s(buff, sizeof(buff), "%%%2X", c);
	return string((char*)buff);
}

std::string escapeHtml(const std::string& s) {
	string d;
	for(string::const_iterator it = s.begin(); it != s.end(); it++) {
		d += toHtml(*it);
	}
	return d;
}