#include "CoffLibraryLoader.h"

#include <windows.h>
#include <string>
#include "PartList.h"
#include "CoffObjectLoader.h"
#include "Hunk.h"
#include "misc.h"
#include "NameMangling.h"
#include "Symbol.h"

using namespace std;

bool CoffLibraryLoader::Clicks(const char* data, int size) const {
	if (size < 8 + 16 + 60 + 16 + (size & 1)) {
		return false;
	}

	// Check signature
	if (memcmp("!<arch>\n", data, 8)) {
		return false;
	}

	// Check first linker member
	const char* ptr = data + 8;
	if (memcmp(ptr, "/               ", 16)) {
		return false;
	}

	return true;
}

bool CoffLibraryLoader::Load(PartList& parts, const char* data, int size, const char* module, bool inLibrary) {
	// Assume that the header and first linker member are fine (as it is checked by click)
	
	CoffObjectLoader coffLoader;

	const char* ptr = data + 8;
	// Skip first linker member
	int memberSize = atoi(&ptr[48]);
	const char *next = ptr + 60 + memberSize;
	next += memberSize & 1;

	int numberOfSymbols;
	int numberOfMembers;
	const int* offsets;
	unsigned short* indices;
	if (memcmp(next, "/               ", 16)) {
		//only first linker member present
		ptr += 60;

		numberOfSymbols = ReadBigEndian((const unsigned char*)ptr);
		ptr += sizeof(int);
		numberOfMembers = numberOfSymbols;

		offsets = ((int*)ptr);
		ptr += numberOfMembers * sizeof(int);

		indices = nullptr;
	} else {
		// Second linker member
		ptr = next + 60;

		numberOfMembers = *((int*)ptr);
		ptr += sizeof(int);

		offsets = ((int*)ptr);
		ptr += numberOfMembers * sizeof(int);
		numberOfSymbols = *((int*)ptr);
		ptr += sizeof(int);

		indices = (unsigned short*)ptr;
		ptr += numberOfSymbols * sizeof(unsigned short);
	}

	// Make symbol names table
	vector<const char*> symbolNames(numberOfSymbols);
	for(int i = 0; i < numberOfSymbols; i++) {
		symbolNames[i] = ptr;
		ptr += strlen(ptr) + 1;
	}

	// Add COFF
	int prev_offset = 0;
	for(int i = 0; i < numberOfMembers; i++) {
		char memberModuleName[256];
		sprintf_s(memberModuleName, 256, "%s|%d", module, i);
		int offset = indices ? offsets[i] : ReadBigEndian((const unsigned char*)&offsets[i]);
		if (offset == 0 || offset == prev_offset) continue;
		prev_offset = offset;
		ptr = data + offset;
		ptr += 60;	// Skip member header

		// COFF
		if(*(int*)ptr != 0xFFFF0000) {
			coffLoader.Load(parts, ptr, 0, memberModuleName, true);
		}
	}

	// Add imports
	for(int i = 0; i < numberOfSymbols; i++) {
		int idx = indices ? indices[i] - 1 : i;
		int offset = indices ? offsets[idx] : ReadBigEndian((const unsigned char*)&offsets[idx]);
		if (offset == 0) continue;
		ptr = data + offset;
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
					importName = StripSymbolPrefix(importName.c_str());
					break;
				case IMPORT_OBJECT_NAME:
					break;
				case IMPORT_OBJECT_NAME_UNDECORATE:
				default:
					importName = UndecorateSymbolName(importName.c_str());
					break;
			}

			if(strlen(symbolNames[i]) >= 6 && memcmp(symbolNames[i], "__imp_", 6) == 0) {
				// An import
				char dllName[256] = {};
				for(int j = 0; importDLL[j] && importDLL[j] != '.'; j++)
					dllName[j] = (char)tolower(importDLL[j]);

				Hunk* hunk = new Hunk(symbolNames[i], importName.c_str(), dllName);
				hunk->MarkHunkAsLibrary();
				parts.GetUninitializedPart().AddHunkBack(hunk);
			} else {
				// A call stub
				Hunk* hunk = MakeCallStub(symbolNames[i]);
				hunk->MarkHunkAsLibrary();
				parts.GetCodePart().AddHunkBack(hunk);
			}
		}
	}
	return true;
}

Hunk* MakeCallStub(const char* name) {
	unsigned char stubData[6] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
	string hunkName = string("stub_for_") + name;
	Hunk* stubHunk = new Hunk(hunkName.c_str(), (char*)stubData, HUNK_IS_CODE, 1, 6, 6);
	stubHunk->AddSymbol(new Symbol(name, 0, SYMBOL_IS_RELOCATEABLE, stubHunk));

	Relocation r;
	r.symbolname += string("__imp_") + name;
	r.offset = 2;
	r.type = RELOCTYPE_ABS32;
	stubHunk->AddRelocation(r);

	return stubHunk;
}
