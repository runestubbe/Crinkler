#ifndef _SYMBOL_H_
#define _SYMBOL_H_

#define SYMBOL_IS_RELOCATEABLE	0x01
#define SYMBOL_IS_LOCAL			0x02

#include <string>

class Hunk;
class Symbol {
public:
	Symbol(const char* name, int value, unsigned int flags, Hunk* hunk);
	std::string		name;
	int				value;
	unsigned int	flags;
	Hunk*			hunk;

	std::string getPrettyName();
};

#endif