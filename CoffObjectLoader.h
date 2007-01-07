#pragma once
#ifndef _COFF_OBJECT_LOADER_H_
#define _COFF_OBJECT_LOADER_H_

#include <windows.h>
#include <string>
#include "HunkLoader.h"

class HunkList;
class CoffObjectLoader : public HunkLoader {
	int getAlignmentBitsFromCharacteristics(int chars) const;
	std::string getSectionName(const IMAGE_SECTION_HEADER* section, const char* stringTable) const;
	std::string getSymbolName(const IMAGE_SYMBOL* symbol, const char* stringTable) const;
public:
	virtual ~CoffObjectLoader();

	virtual bool clicks(const char* data, int size);
	virtual HunkList* load(const char* data, int size, const char* origin);
};

#endif