#include <cassert>
#include "MemoryFile.h"
#include "HunkLoader.h"

bool HunkLoader::LoadFromFile(PartList& parts, const char* filename) {
	MemoryFile mf(filename);
	return Load(parts, mf.GetPtr(), mf.GetSize(), filename, false);
}