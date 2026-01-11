#pragma once
#ifndef _LTCG_LOADER_H_
#define _LTCG_LOADER_H_

#include "HunkLoader.h"

class LTCGLoader : public HunkLoader {
public:
	virtual bool	Clicks(const char* data, int size) const;
	virtual bool	Load(Part& part, const char* data, int size, const char* module, bool inLibrary = false);
};

#endif