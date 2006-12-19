#ifndef _COFF_OBJECT_LOADER_H_
#define _COFF_OBJECT_LOADER_H_

#include <windows.h>
#include <string>
#include "HunkLoader.h"
class HunkList;

class CoffObjectLoader : public HunkLoader {
	int getAlignmentBitsFromCharacteristics(int chars);
	std::string getSectionName(const IMAGE_SECTION_HEADER* section, const char* stringTable);
	std::string getSymbolName(const IMAGE_SYMBOL* symbol, const char* stringTable);
public:
	virtual ~CoffObjectLoader();

	virtual bool clicks(const char* data, int size);
	virtual HunkList* load(const char* data, int size, const char* origin);
};

#endif