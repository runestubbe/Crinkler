#pragma once
#ifndef _STRING_MISC_H_
#define _STRING_MISC_H_

#include <string>
#include <vector>

typedef std::pair<std::string, std::string> StringPair;

std::string toUpper(const std::string& s);
std::string toLower(const std::string& s);
std::string stripPath(const std::string& s);
std::string escapeHtml(const std::string& s);
bool startsWith(const char* str, const char* start);
bool endsWith(const char* str, const char* ending);

std::vector<std::string> intoLines(const char *data, int length);
#endif
