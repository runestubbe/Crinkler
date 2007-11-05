#pragma once
#ifndef _SYMBOL_H_
#define _SYMBOL_H_

const int SYMBOL_IS_RELOCATEABLE =	0x01;
const int SYMBOL_IS_LOCAL =			0x02;
const int SYMBOL_IS_FUNCTION =		0x04;
const int SYMBOL_IS_SECTION =		0x08;

#include <string>

class Hunk;
class Symbol {
public:
	Symbol(const char* name, int value, unsigned int flags, Hunk* hunk, const char* miscString=0);
	std::string		name;
	std::string		secondaryName;	//if this is != "" the symbol is a reference to the symbol with the name secondaryName.
	std::string		miscString;		//for holding extra textual information about the symbol e.g. a section name.
	int				value;
	unsigned int	flags;
	Hunk*			hunk;
	int				size;

	//unmangles the vc decorations
	std::string getUndecoratedName();
};

#endif
