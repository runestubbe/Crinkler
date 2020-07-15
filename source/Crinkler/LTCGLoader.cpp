#include "LTCGLoader.h"
#include "Log.h"

bool LTCGLoader::Clicks(const char* data, int size) const {
	if(size >= 8) {
		const unsigned int* p = (const unsigned int*)data;
		return p[0] == 0xFFFF0000 && p[1] == 0x014C0001;
	}
	return false;
}

HunkList* LTCGLoader::Load(const char* data, int size, const char* module) {
	Log::Error(module, "Link-time code generation is not supported. Disable the 'Whole Program Optimization' option.");
	return false;
}