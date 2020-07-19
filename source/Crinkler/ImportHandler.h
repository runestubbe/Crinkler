#pragma once
#ifndef _IMPORT_HANDLER_H_
#define _IMPORT_HANDLER_H_

#include <vector>
#include <string>
#include <map>
#include <functional>

class Hunk;
class HunkList;
class ImportHandler {
public:
	static HunkList* CreateImportHunks(HunkList* hunklist, Hunk*& hashHunk, std::map<std::string, std::string>& fallbackDlls, const std::vector<std::string>& rangeDlls, bool verbose, bool& usesRangeImport);
	static HunkList* CreateImportHunks1K(HunkList* hunklist, bool verbose, int& hash_bits, int& max_dll_name_length);
};

void ForEachExportInDLL(const char *dll, std::function<void(const char*)> fun);

#endif