#ifndef _IMPORT_HANDLER_H_
#define _IMPORT_HANDLER_H_

#include <vector>
#include <string>

class Hunk;
class HunkList;
class ImportHandler {
public:
	static HunkList* createImportHunks(HunkList* hunklist, Hunk* hashHunk, const std::vector<std::string>& rangeDlls, bool verbose);
};

#endif