#pragma once
#ifndef _SYMBOL_H_
#define _SYMBOL_H_

#define SYMBOL_IS_RELOCATEABLE	0x01
#define SYMBOL_IS_LOCAL			0x02
#define SYMBOL_IS_FUNCTION		0x04

#include <string>

class Hunk;
class Symbol {
public:
	Symbol(const char* name, int value, unsigned int flags, Hunk* hunk);
	std::string		name;
	std::string		secondaryName;	//if this is != "" the symbol is a reference to the symbol with the name secondaryName
	int				value;
	unsigned int	flags;
	Hunk*			hunk;
	int				size;

	std::string getPrettyName();
	std::string getUndecoratedName();
};

#endif
