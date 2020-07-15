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

HunkList* MultiLoader::Load(const char* data, int size, const char* module) {
	for(HunkLoader* loader : m_loaders) {
		if(loader->Clicks(data, size))
			return loader->Load(data, size, module);
	}
	
	return NULL;
}