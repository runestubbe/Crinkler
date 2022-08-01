#pragma once
#ifndef _MULTI_LOADER_H_
#define _MULTI_LOADER_H_

#include <vector>
#include "HunkLoader.h"

class MultiLoader : public HunkLoader {
	std::vector<HunkLoader*>	m_loaders;
public:
	MultiLoader();
	~MultiLoader();

	bool	Clicks(const char* data, int size) const;
	bool	Load(PartList& parts, const char* data, int size, const char* module, bool inLibrary = false);
};

#endif
