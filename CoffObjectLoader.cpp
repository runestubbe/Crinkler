#include <windows.h>
#include <cstdio>

#include "Hunk.h"
#include "HunkList.h"
#include "CoffObjectLoader.h"

using namespace std;

int CoffObjectLoader::getAlignmentBitsFromCharacteristics(int chars) {
	return max(((chars & 0x00F00000)>>20) - 1, 0);
}

string CoffObjectLoader::getSectionName(const IMAGE_SECTION_HEADER* section, const char* stringTable) {
	char str[9];
	memset(str, 0, 9*sizeof(char));
	memcpy(str, section->Name, 8*sizeof(char));

	if(section->Name[0] == '/') {
		int offset = atoi(&str[1]);
		return string(&stringTable[offset]);
	} else {
		return string(str);
	}

}

string CoffObjectLoader::getSymbolName(const IMAGE_SYMBOL* symbol, const char* stringTable) {
	if(symbol->N.Name.Short == 0) {	//long name
		return string(&stringTable[symbol->N.Name.Long]);
	} else {	//short name
		char tmp[9]; tmp[8] = 0;
		memcpy(tmp, symbol->N.ShortName, 8);
		return string(tmp);
	}
}

CoffObjectLoader::~CoffObjectLoader() {
}

bool CoffObjectLoader::clicks(const char* data, int size) {
	//TODO: implement a more safe check
	return *(unsigned short*)data == IMAGE_FILE_MACHINE_I386;
}

HunkList* CoffObjectLoader::load(const char* data, int size, const char* module) {
	//TODO: insert sanity checks
	const char* ptr = data;

	//header
	IMAGE_FILE_HEADER* header = (IMAGE_FILE_HEADER*)ptr;
	ptr += sizeof(IMAGE_FILE_HEADER);

	//symbol table pointer
	IMAGE_SYMBOL* symbolTable = (IMAGE_SYMBOL*)(data + header->PointerToSymbolTable);
	char* stringTable = (char*)symbolTable + header->NumberOfSymbols*sizeof(IMAGE_SYMBOL);

	//section headers
	IMAGE_SECTION_HEADER* sectionHeaders = (IMAGE_SECTION_HEADER*)ptr;

	Hunk* constantsHunk = NULL;
	HunkList* hunklist = new HunkList;
	
	//load sections
	for(int i = 0; i < header->NumberOfSections; i++) {
		string sectionName = getSectionName(&sectionHeaders[i], stringTable);
		int chars = sectionHeaders[i].Characteristics;
		char hunkName[256];
		sprintf_s(hunkName, 256, "h[%s]%s(%d)", module, sectionName.c_str(), i);
		unsigned int flags = 0;
		if(chars & IMAGE_SCN_CNT_CODE)
			flags |= HUNK_IS_CODE;
		if(chars & IMAGE_SCN_MEM_WRITE)
			flags |= HUNK_IS_WRITEABLE;
		bool isInitialized = (chars & IMAGE_SCN_CNT_INITIALIZED_DATA || 
								chars & IMAGE_SCN_CNT_CODE);
		Hunk* hunk = new Hunk(hunkName, data+sectionHeaders[i].PointerToRawData,	//data pointer
								flags, getAlignmentBitsFromCharacteristics(chars),	//alignment
								isInitialized ? sectionHeaders[i].SizeOfRawData : 0,
								sectionHeaders[i].SizeOfRawData);	//virtual size
		hunklist->addHunkBack(hunk);

		//relocations
		IMAGE_RELOCATION* relocs = (IMAGE_RELOCATION*) (data + sectionHeaders[i].PointerToRelocations);
		int nRelocs = sectionHeaders[i].PointerToRelocations ? sectionHeaders[i].NumberOfRelocations : 0;
		for(int j = 0; j < nRelocs; j++) {
			relocation r;
			IMAGE_SYMBOL* symbol = &symbolTable[relocs[j].SymbolTableIndex];
			string symbolName = getSymbolName(symbol, stringTable);
			if(symbol->StorageClass == IMAGE_SYM_CLASS_STATIC || 
				symbol->StorageClass == IMAGE_SYM_CLASS_LABEL) {	//local symbol reference
				//construct local name in 'module!symbolname' format.
				char name[256];
				sprintf_s(name, 256, "l[%s]%s", module, symbolName.c_str());
				r.symbolname = name;
			} else {
				r.symbolname = symbolName;
			}
			r.offset = relocs[j].VirtualAddress;
			switch(relocs[j].Type) {
				case IMAGE_REL_I386_DIR32NB:	//TODO: what is this?
				case IMAGE_REL_I386_DIR32:
					r.type = RELOCTYPE_ABS32;
					break;
				case IMAGE_REL_I386_REL32:
					r.type = RELOCTYPE_REL32;
			}
			
			hunk->addRelocation(r);
		}
	}

	//symbols
	for(int i = 0; i < (int)header->NumberOfSymbols; i++) {
		IMAGE_SYMBOL* sym = &symbolTable[i];
	
		//skip unknown symbol types
		if(sym->StorageClass != IMAGE_SYM_CLASS_EXTERNAL &&
			sym->StorageClass != IMAGE_SYM_CLASS_STATIC &&
			sym->StorageClass != IMAGE_SYM_CLASS_LABEL) {
				i += sym->NumberOfAuxSymbols;
				continue;
		}

		Symbol* s = new Symbol(getSymbolName(sym, stringTable).c_str(), sym->Value, SYMBOL_IS_RELOCATEABLE, 0);

		if(sym->SectionNumber > 0) {
			s->hunk = (*hunklist)[sym->SectionNumber-1];

			if(sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->Type == 0x20 && sym->NumberOfAuxSymbols > 0) {	//function definition
				IMAGE_AUX_SYMBOL* aux = (IMAGE_AUX_SYMBOL*) (sym+1);
				s->flags |= SYMBOL_IS_FUNCTION;
				s->size = aux->Sym.Misc.TotalSize;
			}

			if(sym->StorageClass == IMAGE_SYM_CLASS_STATIC ||	//perform name mangling on local symbols
				sym->StorageClass == IMAGE_SYM_CLASS_LABEL) {
					char symname[256];
					sprintf_s(symname, 256, "l[%s]%s", module, s->name.c_str());
					s->name = symname;
					s->flags |= SYMBOL_IS_LOCAL;
			}
			s->hunk->addSymbol(s);
		} else if(sym->SectionNumber == 0 && sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->Value > 0) {
			//create an uninitialised hunk
			char hunkName[256];
			sprintf_s(hunkName, 256, "u[%s]%s", module, s->name.c_str());
			Hunk* uninitHunk = new Hunk(hunkName, NULL, HUNK_IS_WRITEABLE, 1, 0, s->value);
			s->hunk = uninitHunk;
			s->value = 0;
			uninitHunk->addSymbol(s);
			hunklist->addHunkBack(uninitHunk);
		} else if(sym->SectionNumber == -1) {
			if(constantsHunk == NULL) {
				char hunkName[256];
				sprintf_s(hunkName, 256, "c[%s]constants", module);
				constantsHunk = new Hunk(hunkName, 0, 0, 1, 0, 0);
				hunklist->addHunkBack(constantsHunk);
			}

			s->hunk = constantsHunk;
			s->flags = 0;
			constantsHunk->addSymbol(s);
		} else {
			delete s;
		}
		i += sym->NumberOfAuxSymbols;
	}

	//trim hunks
	hunklist->trim();

	return hunklist;
}