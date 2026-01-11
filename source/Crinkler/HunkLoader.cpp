#include <cassert>
#include "MemoryFile.h"
#include "HunkLoader.h"

bool HunkLoader::LoadFromFile(Part& part, const char* filename) {
	MemoryFile mf(filename);
	return Load(part, mf.GetPtr(), mf.GetSize(), filename, false);
}