#pragma once
#ifndef _STRING_MISC_H_
#define _STRING_MISC_H_

#include <string>

typedef std::pair<std::string, std::string> StringPair;

std::string toUpper(const std::string& s);
std::string toLower(const std::string& s);
std::string stripPath(const std::string& s);
std::string escapeHtml(const std::string& s);
#endif
