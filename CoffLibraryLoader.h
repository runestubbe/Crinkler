#pragma once
#ifndef _COFF_LIBRARY_LOADER_H_
#define _COFF_LIBRARY_LOADER_H_

#include <windows.h>
#include <string>
#include "HunkLoader.h"
class HunkList;

class CoffLibraryLoader : public HunkLoader {
public:
	virtual ~CoffLibraryLoader();

	virtual bool clicks(const char* data, int size);
	virtual HunkList* load(const char* data, int size, const char* origin);
};

#endif