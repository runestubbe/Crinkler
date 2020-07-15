#include "CoffObjectLoader.h"

#include <windows.h>
#include "Hunk.h"
#include "HunkList.h"
#include "Symbol.h"
#include "StringMisc.h"

using namespace std;

static int GetAlignmentBitsFromCharacteristics(int chars) {
	return max(((chars & 0x00F00000)>>20) - 1, 0);
}

static string GetSectionName(const IMAGE_SECTION_HEADER* section, const char* stringTable) {
	char tmp[9]; tmp[8] = 0;
	memcpy(tmp, section->Name, 8*sizeof(char));

	if(section->Name[0] == '/') {
		int offset = atoi(&tmp[1]);
		return string(&stringTable[offset]);
	} else {
		return tmp;
	}

}

static string GetSymbolName(const IMAGE_SYMBOL* symbol, const char* stringTable) {
	if(symbol->N.Name.Short == 0) {	// Long name
		return &stringTable[symbol->N.Name.Long];
	} else {	// Short name
		char tmp[9]; tmp[8] = 0;
		memcpy(tmp, symbol->N.ShortName, 8);
		return tmp;
	}
}

static string StripNumeral(const string& s) {
	int idx = (int)s.size()-1;
	while(idx >= 0 && s[idx] != '|') idx--;
	if (idx == 0) return s;
	return s.substr(0, idx);
}

CoffObjectLoader::~CoffObjectLoader() {
}

bool CoffObjectLoader::Clicks(const char* data, int size) const {
	//TODO: Implement a safer check
	return *(unsigned short*)data == IMAGE_FILE_MACHINE_I386;
}

HunkList* CoffObjectLoader::Load(const char* data, int size, const char* module) {
	const char* ptr = data;

	// Header
	const IMAGE_FILE_HEADER* header = (const IMAGE_FILE_HEADER*)ptr;
	ptr += sizeof(IMAGE_FILE_HEADER);

	// Symbol table pointer
	const IMAGE_SYMBOL* symbolTable = (const IMAGE_SYMBOL*)(data + header->PointerToSymbolTable);
	const char* stringTable = (const char*)symbolTable + header->NumberOfSymbols*sizeof(IMAGE_SYMBOL);

	// Section headers
	const IMAGE_SECTION_HEADER* sectionHeaders = (const IMAGE_SECTION_HEADER*)ptr;

	HunkList* hunklist = new HunkList;
	Hunk* constantsHunk;
	{
		char hunkName[1000];
		sprintf_s(hunkName, 1000, "c[%s]!constants", module);
		constantsHunk = new Hunk(hunkName, 0, 0, 1, 0, 0);
	}

	
	// Load sections
	for(int i = 0; i < header->NumberOfSections; i++) {
		string sectionName = GetSectionName(&sectionHeaders[i], stringTable);
		int chars = sectionHeaders[i].Characteristics;
		char hunkName[1000];
		sprintf_s(hunkName, 1000, "h[%s](%d)!%s", module, i, sectionName.c_str());
		unsigned int flags = 0;
		if(chars & IMAGE_SCN_CNT_CODE)
			flags |= HUNK_IS_CODE;
		if(chars & IMAGE_SCN_MEM_WRITE)
			flags |= HUNK_IS_WRITEABLE;
		bool isInitialized = (chars & IMAGE_SCN_CNT_INITIALIZED_DATA || 
								chars & IMAGE_SCN_CNT_CODE);
		Hunk* hunk = new Hunk(hunkName, data+sectionHeaders[i].PointerToRawData,	// Data pointer
								flags, GetAlignmentBitsFromCharacteristics(chars),	// Alignment
								isInitialized ? sectionHeaders[i].SizeOfRawData : 0,
								sectionHeaders[i].SizeOfRawData);	// Virtual size
		hunklist->AddHunkBack(hunk);

		// Relocations
		const IMAGE_RELOCATION* relocs = (const IMAGE_RELOCATION*) (data + sectionHeaders[i].PointerToRelocations);
		int nRelocs = sectionHeaders[i].PointerToRelocations ? sectionHeaders[i].NumberOfRelocations : 0;
		for(int j = 0; j < nRelocs; j++) {
			Relocation r;
			int symbolIndex = relocs[j].SymbolTableIndex;
			const IMAGE_SYMBOL* symbol = &symbolTable[symbolIndex];
			string symbolName = GetSymbolName(symbol, stringTable);
			if(symbol->StorageClass == IMAGE_SYM_CLASS_STATIC || 
				symbol->StorageClass == IMAGE_SYM_CLASS_LABEL) {	// Local symbol reference
				// Construct local name
				char name[1000];
				sprintf_s(name, 1000, "l[%s(%d)]!%s", module, symbolIndex, symbolName.c_str());
				r.symbolname = name;
			} else {
				r.symbolname = symbolName;
			}
			r.offset = relocs[j].VirtualAddress;
			switch(relocs[j].Type) {
				case IMAGE_REL_I386_DIR32NB:
				case IMAGE_REL_I386_DIR32:
					r.type = RELOCTYPE_ABS32;
					break;
				case IMAGE_REL_I386_REL32:
					r.type = RELOCTYPE_REL32;
			}
			r.objectname = StripNumeral(StripPath(module));
			
			hunk->AddRelocation(r);
		}
	}

	// Symbols
	for(int i = 0; i < (int)header->NumberOfSymbols; i++) {
		const IMAGE_SYMBOL* sym = &symbolTable[i];
	
		// Skip unknown symbol types
		if(sym->StorageClass != IMAGE_SYM_CLASS_EXTERNAL &&
			sym->StorageClass != IMAGE_SYM_CLASS_STATIC &&
			sym->StorageClass != IMAGE_SYM_CLASS_LABEL &&
			sym->StorageClass != IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
				i += sym->NumberOfAuxSymbols;
				continue;
		}

		Symbol* s = new Symbol(GetSymbolName(sym, stringTable).c_str(), sym->Value, SYMBOL_IS_RELOCATEABLE, 0);

		if(sym->SectionNumber > 0) {
			s->hunk = (*hunklist)[sym->SectionNumber-1];

			if(sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->Type == 0x20 && sym->NumberOfAuxSymbols > 0) {	// Function definition
				const IMAGE_AUX_SYMBOL* aux = (const IMAGE_AUX_SYMBOL*) (sym+1);
				s->flags |= SYMBOL_IS_FUNCTION;
				s->size = aux->Sym.Misc.TotalSize;
			}

			if(sym->StorageClass == IMAGE_SYM_CLASS_STATIC ||	// Perform name mangling on local symbols
				sym->StorageClass == IMAGE_SYM_CLASS_LABEL) {

				char symname[1000];
				sprintf_s(symname, 1000, "l[%s(%d)]!%s", module, i, s->name.c_str());
				s->name = symname;
				s->flags |= SYMBOL_IS_LOCAL;
				if(sym->StorageClass == IMAGE_SYM_CLASS_STATIC && sym->NumberOfAuxSymbols == 1) {
					s->flags |= SYMBOL_IS_SECTION;
					s->miscString = module;
				}
			}
			s->hunk->AddSymbol(s);
		} else if(sym->SectionNumber == 0 && sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->Value > 0) {
			// Create an uninitialised hunk
			char hunkName[1000];
			sprintf_s(hunkName, 1000, "u[%s]!%s", module, s->name.c_str());
			Hunk* uninitHunk = new Hunk(hunkName, NULL, HUNK_IS_WRITEABLE, 1, 0, s->value);
			s->hunk = uninitHunk;
			s->value = 0;
			uninitHunk->AddSymbol(s);
			hunklist->AddHunkBack(uninitHunk);
		} else if(sym->SectionNumber == 0 && sym->StorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL && sym->Value == 0) {
			// Weak external
			const IMAGE_AUX_SYMBOL* aux = (const IMAGE_AUX_SYMBOL*) (sym+1);
			s->secondaryName = GetSymbolName(&symbolTable[aux->Sym.TagIndex], stringTable);
			s->hunk = constantsHunk;
			s->flags = 0;
			s->hunk->AddSymbol(s);
		} else if(sym->SectionNumber == -1) {	// Constant symbol
			s->hunk = constantsHunk;
			s->flags = 0;
			s->hunk->AddSymbol(s);
		} else {
			// Ignore unknown symbol type
			delete s;
		}
		

		i += sym->NumberOfAuxSymbols;	// Skip aux symbols
	}

	// Trim hunks
	hunklist->AddHunkBack(constantsHunk);
	hunklist->Trim();

	return hunklist;
}