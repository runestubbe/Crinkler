#pragma once
#ifndef _IMPORT_HANDLER_H_
#define _IMPORT_HANDLER_H_

#include <vector>
#include <string>
#include <map>
#include <functional>

class Hunk;
class PartList;
class ImportHandler {
public:

	static void AddImportHunks1K(PartList& parts, bool verbose, int& hash_bits, int& max_dll_name_length);
	static void AddImportHunks4K(PartList& parts, Hunk*& hashHunk, std::map<std::string, std::string>& fallbackDlls, const std::vector<std::string>& rangeDlls, bool verbose, bool& usesRangeImport);
};

void ForEachExportInDLL(const char *dll, std::function<void(const char*)> fun);

#endif