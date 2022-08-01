#pragma once
#ifndef _COFF_OBJECT_LOADER_H_
#define _COFF_OBJECT_LOADER_H_

#include "HunkLoader.h"

class PartList;
class CoffObjectLoader : public HunkLoader {
public:
	virtual ~CoffObjectLoader();

	virtual bool	Clicks(const char* data, int size) const;
	virtual bool	Load(PartList& parts, const char* data, int size, const char* module, bool inLibrary = false);
};

#endif
