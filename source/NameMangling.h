#pragma once
#ifndef _NAME_MANGLING_H_
#define _NAME_MANGLING_H_

#include <string>

//strips a symbolname of its ?@_ prefix
std::string stripSymbolPrefix(const char* str);

//strips a symbolname of its crinkler prefixes
std::string stripCrinklerSymbolPrefix(const char* str);

//undecorates a symbolname
//strips both normal- and crinkler-prefixes and any '@'-suffix
std::string undecorateSymbolName(const char* str);

#endif
