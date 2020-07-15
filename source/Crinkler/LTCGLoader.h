#pragma once
#ifndef _LTCG_LOADER_H_
#define _LTCG_LOADER_H_

#include "HunkLoader.h"
class HunkList;

class LTCGLoader : public HunkLoader {
public:
	virtual bool		Clicks(const char* data, int size) const;
	virtual HunkList*	Load(const char* data, int size, const char* module);
};

#endif