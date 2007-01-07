#include <string>
#include "HunkList.h"
#include "CoffLibraryLoader.h"
#include "CoffObjectLoader.h"
#include "Hunk.h"
#include "NameMangling.h"
#include "Symbol.h"

using namespace std;

CoffLibraryLoader::~CoffLibraryLoader() {
}

bool CoffLibraryLoader::clicks(const char* data, int size) {
	const char* ptr = data;

	if(size < 8+16+60+16+(size&1))
		return false;

	//check signature
	if(memcmp("!<arch>\n", ptr, 8*sizeof(char))) {
		return false;
	}
	ptr += 8;

	//check 1st linker member
	if(memcmp(ptr, "/               ", 16))
		return false;

	{	//skip member
		int size = atoi(&ptr[48]);
		ptr += 60 + size;
		if(size & 1)
			ptr++;
	}

	//check 2nd linker member
	if(memcmp(ptr, "/               ", 16))
		return false;

	return true;
}

HunkList* CoffLibraryLoader::load(const char* data, int size, const char* module) {
	//assume that all initial headers are fine (as it is checked by click)
	
	HunkList* hunklist = new HunkList;
	const char* ptr = data+8;
	{	//skip member
		int size = atoi(&ptr[48]);
		ptr += 120 + size;
		if(size & 1)
			ptr++;
	}

	//second linker member
	int numberOfMembers = *((unsigned int*)ptr);
	ptr += sizeof(int);

	unsigned int* offsets = ((unsigned int*)ptr);
	ptr += (numberOfMembers)*sizeof(int);
	int numberOfSymbols = *((unsigned int*)ptr);
	ptr += sizeof(unsigned int);

	unsigned short* indices = (unsigned short*)ptr;
	ptr += numberOfSymbols * sizeof(unsigned short);

	//make symbol names table
	const char** symbolNames = new const char*[numberOfSymbols];
	for(int i = 0; i < numberOfSymbols; i++) {
		symbolNames[i] = ptr;
		ptr += strlen(ptr) + 1;
	}

	//add coff
	for(int i = 0; i < numberOfMembers; i++) {
		char memberName[256];
		sprintf_s(memberName, 256, "%s#%d", module, i);
		ptr = data + offsets[i];
		ptr += 60;	//skip member header

		if(*(int*)ptr != 0xFFFF0000) {	//coff
			CoffObjectLoader coffLoader;
			HunkList* hl = coffLoader.load(ptr, 0, memberName);
			hunklist->append(hl);
			delete hl;
		}
	}

	//add imports
	for(int i = 0; i < numberOfSymbols; i++) {
		int idx = indices[i] - 1;
		ptr = data + offsets[idx];
		ptr += 60;

		if(*(int*)ptr == 0xFFFF0000) {	//import
			unsigned short flags = *((unsigned short*) &ptr[18]);
			unsigned int nameType = (flags >> 2) & 7;

			ptr += 20;
			string importName = ptr;
			ptr += strlen(ptr) + 1;
			const char* importDLL = ptr;

			switch(nameType) {
				case IMPORT_OBJECT_NAME_NO_PREFIX:
					importName = removePrefix(importName.c_str());
					break;
				case IMPORT_OBJECT_NAME:
					break;
				case IMPORT_OBJECT_NAME_UNDECORATE:
				default:
					importName = undecorate(importName.c_str());
					break;
			}

			if(strlen(symbolNames[i]) >= 6 && memcmp(symbolNames[i], "__imp_", 6) == 0) {	//TODO: this is an EVIL hack
				//an import
				char dllName[256];
				memset(dllName, 0, 256*sizeof(char));
				for(int j = 0; importDLL[j] && importDLL[j] != '.'; j++)
					dllName[j] = (char)tolower(importDLL[j]);

				Hunk* importHunk = new Hunk(symbolNames[i], importName.c_str(), dllName);
				hunklist->addHunkBack(importHunk);
			} else {	//a call stub
				unsigned char stubData[6] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
				char hunkName[512];
				sprintf_s(hunkName, 512, "stub_for_%s", symbolNames[i]);
				
				Hunk* stubHunk = new Hunk(hunkName, (char*)stubData, HUNK_IS_CODE, 1, 6, 6);
				hunklist->addHunkBack(stubHunk);
				stubHunk->addSymbol(new Symbol(symbolNames[i], 0, SYMBOL_IS_RELOCATEABLE, stubHunk));
				
				relocation r;
				if(strlen(symbolNames[i]) >= 6 && memcmp("__imp_", symbolNames[i], 6) == 0) {
					r.symbolname = symbolNames[i];
				} else {
					r.symbolname += string("__imp_") + symbolNames[i];	//TODO: hackedy hack
				}
				
				r.offset = 2;
				r.type = RELOCTYPE_ABS32;
				stubHunk->addRelocation(r);
			}
		}
	}

	delete[] symbolNames;
	return hunklist;
}
