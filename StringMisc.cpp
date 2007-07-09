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