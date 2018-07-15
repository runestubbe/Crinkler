#include <cassert>
#include "MemoryFile.h"
#include "HunkLoader.h"

bool HunkLoader::clicksFromFile(const char* filename) {
	MemoryFile mf(filename);
	assert(mf.getPtr());

	return clicks(mf.getPtr(), mf.getSize());
}


HunkList* HunkLoader::loadFromFile(const char* filename) {
	MemoryFile mf(filename);
	assert(mf.getPtr());

	return load(mf.getPtr(), mf.getSize(), filename);
}