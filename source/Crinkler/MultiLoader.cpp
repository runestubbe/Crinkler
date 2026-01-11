#include "MultiLoader.h"
#include "CoffLibraryLoader.h"
#include "CoffObjectLoader.h"
#include "LTCGLoader.h"

using namespace std;

MultiLoader::MultiLoader() {
	m_loaders.push_back(new CoffLibraryLoader);
	m_loaders.push_back(new CoffObjectLoader);
	m_loaders.push_back(new LTCGLoader);
}

MultiLoader::~MultiLoader() {
	//free loaders
	for(HunkLoader* loader : m_loaders)
		delete loader;
}

bool MultiLoader::Clicks(const char* data, int size) const {
	for(HunkLoader* loader : m_loaders) {
		if(loader->Clicks(data, size))
			return true;
	}
	return false;
}

bool MultiLoader::Load(Part& part, const char* data, int size, const char* module, bool inLibrary) {
	for(HunkLoader* loader : m_loaders) {
		if(loader->Clicks(data, size))
			return loader->Load(part, data, size, module, inLibrary);
	}
	return false;
}