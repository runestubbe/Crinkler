#pragma once
#ifndef _COFF_LIBRARY_LOADER_H_
#define _COFF_LIBRARY_LOADER_H_

#include "HunkLoader.h"

class Hunk;

class CoffLibraryLoader : public HunkLoader {
public:
	virtual bool	Clicks(const char* data, int size) const;
	virtual bool	Load(Part& part, const char* data, int size, const char* module, bool inLibrary = false);
};

Hunk* MakeCallStub(const char* name);

#endif
