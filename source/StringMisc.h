#pragma once
#ifndef _STRING_MISC_H_
#define _STRING_MISC_H_

#include <string>
#include <vector>

typedef std::pair<std::string, std::string> StringPair;

std::string ToUpper(const std::string& s);
std::string ToLower(const std::string& s);
std::string StripPath(const std::string& s);
std::string EscapeHtml(const std::string& s);
bool StartsWith(const char* str, const char* start);
bool EndsWith(const char* str, const char* ending);

std::vector<std::string> IntoLines(const char *data, int length);
#endif
