#include "CoffLibraryLoader.h"

#include <windows.h>
#include <string>
#include "HunkList.h"
#include "CoffObjectLoader.h"
#include "Hunk.h"
#include "NameMangling.h"
#include "Symbol.h"

using namespace std;

bool CoffLibraryLoader::clicks(const char* data, int size) {
	const char* ptr = data;

	if(size < 8+16+60+16+(size&1))
		return false;

	// Check signature
	if(memcmp("!<arch>\n", ptr, 8*sizeof(char))) {
		return false;
	}
	ptr += 8;

	// Check 1st linker member
	if(memcmp(ptr, "/               ", 16))
		return false;

	{	// Skip member
		int memberSize = atoi(&ptr[48]);
		ptr += 60 + memberSize;
		if(memberSize & 1)
			ptr++;
	}

	// Check 2nd linker member
	if(memcmp(ptr, "/               ", 16))
		return false;

	return true;
}

HunkList* CoffLibraryLoader::load(const char* data, int size, const char* module) {
	// Assume that all initial headers are fine (as it is checked by click)
	
	HunkList* hunklist = new HunkList;
	const char* ptr = data+8;
	{	// Skip member
		int memberSize = atoi(&ptr[48]);
		ptr += 120 + memberSize;
		if(memberSize & 1)
			ptr++;
	}

	// Second linker member
	int numberOfMembers = *((unsigned int*)ptr);
	ptr += sizeof(int);

	unsigned int* offsets = ((unsigned int*)ptr);
	ptr += (numberOfMembers)*sizeof(int);
	int numberOfSymbols = *((unsigned int*)ptr);
	ptr += sizeof(unsigned int);

	unsigned short* indices = (unsigned short*)ptr;
	ptr += numberOfSymbols * sizeof(unsigned short);

	// Make symbol names table
	vector<const char*> symbolNames(numberOfSymbols);
	for(int i = 0; i < numberOfSymbols; i++) {
		symbolNames[i] = ptr;
		ptr += strlen(ptr) + 1;
	}

	// Add COFF
	for(int i = 0; i < numberOfMembers; i++) {
		char memberModuleName[256];
		sprintf_s(memberModuleName, 256, "%s|%d", module, i);
		if (offsets[i] == 0) continue;
		ptr = data + offsets[i];
		ptr += 60;	// Skip member header

		// COFF
		if(*(int*)ptr != 0xFFFF0000) {
			CoffObjectLoader coffLoader;
			HunkList* hl = coffLoader.load(ptr, 0, memberModuleName);
			hunklist->append(hl);
			delete hl;
		}
	}

	// Add imports
	for(int i = 0; i < numberOfSymbols; i++) {
		int idx = indices[i] - 1;
		if (offsets[idx] == 0) continue;
		ptr = data + offsets[idx];
		ptr += 60;

		if(*(int*)ptr == 0xFFFF0000) {	// Import
			unsigned short flags = *((unsigned short*) &ptr[18]);
			unsigned int nameType = (flags >> 2) & 7;

			ptr += 20;
			string importName = ptr;
			ptr += strlen(ptr) + 1;
			const char* importDLL = ptr;

			switch(nameType) {
				case IMPORT_OBJECT_NAME_NO_PREFIX:
					importName = stripSymbolPrefix(importName.c_str());
					break;
				case IMPORT_OBJECT_NAME:
					break;
				case IMPORT_OBJECT_NAME_UNDECORATE:
				default:
					importName = undecorateSymbolName(importName.c_str());
					break;
			}

			if(strlen(symbolNames[i]) >= 6 && memcmp(symbolNames[i], "__imp_", 6) == 0) {
				// An import
				char dllName[256];
				memset(dllName, 0, 256*sizeof(char));
				for(int j = 0; importDLL[j] && importDLL[j] != '.'; j++)
					dllName[j] = (char)tolower(importDLL[j]);

				hunklist->addHunkBack(new Hunk(symbolNames[i], importName.c_str(), dllName));
			} else {	// A call stub
				unsigned char stubData[6] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
				char hunkName[512];
				sprintf_s(hunkName, 512, "stub_for_%s", symbolNames[i]);
				
				Hunk* stubHunk = new Hunk(hunkName, (char*)stubData, HUNK_IS_CODE, 1, 6, 6);
				hunklist->addHunkBack(stubHunk);
				stubHunk->addSymbol(new Symbol(symbolNames[i], 0, SYMBOL_IS_RELOCATEABLE, stubHunk));
				
				Relocation r;
				if(strlen(symbolNames[i]) >= 6 && memcmp("__imp_", symbolNames[i], 6) == 0) {
					r.symbolname = symbolNames[i];
				} else {
					r.symbolname += string("__imp_") + symbolNames[i];
				}
				
				r.offset = 2;
				r.type = RELOCTYPE_ABS32;
				stubHunk->addRelocation(r);
			}
		}
	}

	hunklist->markHunksAsLibrary();

	return hunklist;
}
