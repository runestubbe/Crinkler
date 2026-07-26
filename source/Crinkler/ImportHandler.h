#pragma once
#ifndef _IMPORT_HANDLER_H_
#define _IMPORT_HANDLER_H_

#include <vector>
#include <string>
#include <map>
#include <functional>

class Hunk;
class Part;
class ImportHandler {
public:

	static void AddImportHunks1K(Part& part, bool verbose, int& hash_bits, int& max_dll_name_length);
	static void AddImportHunks4K(Part& part, Hunk*& hashHunk, Hunk* header, Hunk* headerRefHunk, const std::vector<std::string>& rangeDlls, bool verbose, bool& usesRangeImport, int& dllSkip);
};

void ForEachExportInDLL(const char *dll, std::function<void(const char*)> fun);

#endif