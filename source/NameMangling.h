#pragma once
#ifndef _NAME_MANGLING_H_
#define _NAME_MANGLING_H_

#include <string>

// Strips a symbolname of its ?@_ prefix
std::string StripSymbolPrefix(const char* str);

// Strips a symbolname of its Crinkler prefixes
std::string StripCrinklerSymbolPrefix(const char* str);

// Undecorates a symbolname
// Strips both normal- and Crinkler-prefixes and any '@'-suffix
std::string UndecorateSymbolName(const char* str);

#endif
