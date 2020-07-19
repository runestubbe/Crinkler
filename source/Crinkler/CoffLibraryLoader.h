#pragma once
#ifndef _COFF_LIBRARY_LOADER_H_
#define _COFF_LIBRARY_LOADER_H_

#include "HunkLoader.h"
class Hunk;
class HunkList;

class CoffLibraryLoader : public HunkLoader {
public:
	virtual bool		Clicks(const char* data, int size) const;
	virtual HunkList*	Load(const char* data, int size, const char* module);
};

Hunk* MakeCallStub(const char* name);

#endif
