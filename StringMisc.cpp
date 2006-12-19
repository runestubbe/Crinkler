#include "StringMisc.h"
#include <algorithm>

using namespace std;

string toUpper(const string& s) {
	string d(s);
	transform(d.begin(), d.end(), d.begin(), (int(*)(int))toupper);
	return d;
}
