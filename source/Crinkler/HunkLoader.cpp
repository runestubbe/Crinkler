#include <cassert>
#include "MemoryFile.h"
#include "HunkLoader.h"

HunkList* HunkLoader::LoadFromFile(const char* filename) {
	MemoryFile mf(filename);

	return Load(mf.GetPtr(), mf.GetSize(), filename);
}