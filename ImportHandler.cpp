#include <windows.h>
#include <fstream>
#include "ImportHandler.h"

#include "HunkList.h"
#include "Hunk.h"
#include "StringMisc.h"
#include "Log.h"
#include "Symbol.h"

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>

using namespace std;

char *LoadDLL(const char *name) {
	char* module = (char *)((int)LoadLibraryEx(name, 0, DONT_RESOLVE_DLL_REFERENCES) & -4096);
	if(module == 0) {
		Log::error(0, "", "Could not open DLL '%s'", name);
	}
	return module;
}

int getOrdinal(const char* function, const char* dll) {
	char* module = LoadDLL(dll);

	IMAGE_DOS_HEADER* dh = (IMAGE_DOS_HEADER*)module;
	IMAGE_FILE_HEADER* coffHeader = (IMAGE_FILE_HEADER*)(module + dh->e_lfanew+4);
	IMAGE_OPTIONAL_HEADER32* pe = (IMAGE_OPTIONAL_HEADER32*)(coffHeader+1);
	IMAGE_EXPORT_DIRECTORY* exportdir = (IMAGE_EXPORT_DIRECTORY*) (module + pe->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	
	short* ordinalTable = (short*) (module + exportdir->AddressOfNameOrdinals);
	int* nameTable = (int*)(module + exportdir->AddressOfNames);
	for(int i = 0; i < (int)exportdir->NumberOfNames; i++) {
		int ordinal = ordinalTable[i] + exportdir->Base;
		char* name = module+nameTable[i];
		if(strcmp(name, function) == 0) {
			return ordinal;
		}
	}

	Log::error(0, "", "import '%s' could not be found in '%s'", function, dll);
	return -1;
}

bool isForwardRVA(const char* dll, const char* function) {
	char* module = LoadDLL(dll);
	IMAGE_DOS_HEADER* pDH = (PIMAGE_DOS_HEADER)module;
	IMAGE_NT_HEADERS* pNTH = (PIMAGE_NT_HEADERS)(module + pDH->e_lfanew);
	IMAGE_EXPORT_DIRECTORY* pIED = (PIMAGE_EXPORT_DIRECTORY)(module + pNTH->OptionalHeader.DataDirectory[0].VirtualAddress);

	short* ordinalTable = (short*)(module + pIED->AddressOfNameOrdinals);
	DWORD* namePointerTable = (DWORD*)(module + pIED->AddressOfNames);
	DWORD* addressTableRVAOffset = addressTableRVAOffset = (DWORD*)(module + pIED->AddressOfFunctions);

	for(unsigned int i = 0; i < pIED->NumberOfNames; i++) {
		short ordinal = ordinalTable[i];
		char* name = (char*)(module + namePointerTable[i]);

		if(strcmp(name, function) == 0) {
			DWORD address = addressTableRVAOffset[ordinal];
			if(address >= pNTH->OptionalHeader.DataDirectory[0].VirtualAddress &&
				address < pNTH->OptionalHeader.DataDirectory[0].VirtualAddress + pNTH->OptionalHeader.DataDirectory[0].Size)
				return true;
			return false;
		}
	}

	Log::error(0, "", "import '%s' could not be found in '%s'", function, dll);
	return false;
}


bool importHunkRelation(const Hunk* h1, const Hunk* h2) {
	//sort by dll name
	if(strcmp(h1->getImportDll(), h2->getImportDll()) != 0) {
		//kernel32 always first
		if(strcmp(h1->getImportDll(), "kernel32") == 0)
			return true;
		if(strcmp(h2->getImportDll(), "kernel32") == 0)
			return false;

		//then user32, to ensure MessageBoxA@16 is ready when we need it
		if(strcmp(h1->getImportDll(), "user32") == 0)
			return true;
		if(strcmp(h2->getImportDll(), "user32") == 0)
			return false;


		return strcmp(h1->getImportDll(), h2->getImportDll()) < 0;
	}

	//sort by ordinal
	return getOrdinal(h1->getImportName(), h1->getImportDll()) < 
		getOrdinal(h2->getImportName(), h2->getImportDll());
}

const int hashCode(const char* str) {
	int code = 0;
	char eax;
	do {
		code = _rotl(code, 6);
		eax = *str++;
		code ^= eax;

	} while(eax);
	return code;
}

const unsigned int hashCode_1k(const char* str) {
	unsigned int code = 0;
	unsigned int eax;
	do {
		code = _rotl(code, 6);
		eax = *str++;
		code += eax;
	} while(eax);
	return (code & 0x3FFFFF)*4;
}


HunkList* ImportHandler::createImportHunks(HunkList* hunklist, Hunk* hashHunk, const vector<string>& rangeDlls, bool verbose, bool& enableRangeImport) {
	if(verbose)
		printf("\n-Imports----------------------------------\n");

	vector<Hunk*> importHunks;
	vector<bool> usedRangeDlls(rangeDlls.size());

	//fill list for import hunks
	enableRangeImport = false;
	for(int i = 0; i <hunklist->getNumHunks(); i++) {
		Hunk* hunk = (*hunklist)[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT) {
			if(isForwardRVA(hunk->getImportDll(), hunk->getImportName())) {
				Log::error(0, "", "import '%s' from '%s' uses forwarded RVA. This feature is not supported by crinkler (yet)", 
					hunk->getImportName(), hunk->getImportDll());
			}

			//is the dll a range dll?
			for(int i = 0; i < rangeDlls.size(); i++) {
				if(toUpper(rangeDlls[i]) == toUpper(hunk->getImportDll())) {
					usedRangeDlls[i] = true;
					enableRangeImport = true;
					break;
				}
			}
			importHunks.push_back(hunk);
		}
	}

	//warn about unused range dlls
	{
		for(int i = 0; i < rangeDlls.size(); i++) {
			if(!usedRangeDlls[i]) {
				Log::warning(0, "", "no functions were imported from range dll '%s'", rangeDlls[i].c_str());
			}
		}
	}

	//sort import hunks
	sort(importHunks.begin(), importHunks.end(), importHunkRelation);

	Hunk* importList = new Hunk("ImportListHunk", 0, HUNK_IS_WRITEABLE, 16, 0, 0);
	char dllNames[1024];
	memset(dllNames, 0, 1024);
	char* dllNamesPtr = dllNames+1;
	char* hashCounter = dllNames;
	DWORD* hashptr = (DWORD*)hashHunk->getPtr();
	string currentDllName;
	int pos = 0;
	for(vector<Hunk*>::const_iterator it = importHunks.begin(); it != importHunks.end();) {
		Hunk* importHunk = *it;
		bool useRange = false;

		//is the dll a range dll?
		for(int i = 0; i < rangeDlls.size(); i++) {
			if(toUpper(rangeDlls[i]) == toUpper(importHunk->getImportDll())) {
				usedRangeDlls[i] = true;
				useRange = true;
				break;
			}
		}

		//skip non hashes
		while(*hashptr != 0x48534148) {
			if(enableRangeImport)
				*dllNamesPtr++ = 1;
			hashptr++;
			(*hashCounter)++;
			pos++;
		}

		if(currentDllName.compare(importHunk->getImportDll())) {
			if(strcmp(importHunk->getImportDll(), "kernel32") != 0) {
				strcpy_s(dllNamesPtr, sizeof(dllNames)-(dllNamesPtr-dllNames), importHunk->getImportDll());
				dllNamesPtr += strlen(importHunk->getImportDll()) + 2;
				hashCounter = dllNamesPtr-1;
				*hashCounter = 0;
			}


			currentDllName = importHunk->getImportDll();
			if(verbose)
				printf("%s\n", currentDllName.c_str());
		}

		(*hashCounter)++;
		int hashcode = hashCode(importHunk->getImportName());
		(*hashptr++) = hashcode;
		int startOrdinal = getOrdinal(importHunk->getImportName(), importHunk->getImportDll());
		int ordinal = startOrdinal;

		//add import
		if(verbose) {
			if(useRange)
				printf("  ordinal range {\n  ");
			printf("  %s (ordinal %d, hash %08X)\n", (*it)->getImportName(), startOrdinal, hashcode);
		}

		importList->addSymbol(new Symbol(importHunk->getName(), pos*4, SYMBOL_IS_RELOCATEABLE, importList));
		it++;

		while(useRange && it != importHunks.end() && 	//import the rest of the range
				currentDllName.compare((*it)->getImportDll()) == 0) {
			int o = getOrdinal((*it)->getImportName(), (*it)->getImportDll());

			if(o - startOrdinal >= 254)
				break;

			if(verbose) {
				printf("    %s (ordinal %d)\n", (*it)->getImportName(), o);
			}

			ordinal = o;
			importList->addSymbol(new Symbol((*it)->getName(), (pos+ordinal-startOrdinal)*4, SYMBOL_IS_RELOCATEABLE, importList));
			it++;
		}

		if(verbose && useRange)
			printf("  }\n");

		if(enableRangeImport)
			*dllNamesPtr++ = ordinal - startOrdinal + 1;
		pos += ordinal - startOrdinal + 1;
	}
	*dllNamesPtr++ = -1;
	importList->setVirtualSize(pos*4);
	importList->addSymbol(new Symbol("_ImportList", 0, SYMBOL_IS_RELOCATEABLE, importList));

	//trim header (remove trailing hashes)
	while(hashHunk->getRawSize() >= 4 && *(int*)(hashHunk->getPtr()+hashHunk->getRawSize()-4) == 0x48534148) {
		hashHunk->chop(4);
	}
	hashHunk->setVirtualSize(hashHunk->getRawSize());

	//create new hunklist
	HunkList* newHunks = new HunkList;

	newHunks->addHunkBack(importList);

	Hunk* dllNamesHunk = new Hunk("DllNames", dllNames, HUNK_IS_WRITEABLE, 0, dllNamesPtr - dllNames, dllNamesPtr - dllNames);
	dllNamesHunk->addSymbol(new Symbol("_DLLNames", 0, SYMBOL_IS_RELOCATEABLE, dllNamesHunk));
	newHunks->addHunkBack(dllNamesHunk);

	return newHunks;
}

HunkList* ImportHandler::createImportHunks1K(HunkList* hunklist, bool verbose) {
	if(verbose)
		printf("\n-Imports----------------------------------\n");

	vector<Hunk*> importHunks;
	set<string> dlls;

	//fill list for import hunks
	for(int i = 0; i <hunklist->getNumHunks(); i++) {
		Hunk* hunk = (*hunklist)[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT) {
			dlls.insert(hunk->getImportDll());
			if(isForwardRVA(hunk->getImportDll(), hunk->getImportName())) {
				Log::error(0, "", "import '%s' from '%s' uses forwarded RVA. This feature is not supported by crinkler (yet)", 
					hunk->getImportName(), hunk->getImportDll());
			}
			importHunks.push_back(hunk);
		}
	}

	string dllnames;
	for(set<string>::iterator it = dlls.begin(); it != dlls.end(); it++) {
		if(it->compare("kernel32")) {
			string name = *it;
			name.resize(9);
			dllnames += name;
		}
	}

	Hunk* importList = new Hunk("ImportListHunk", 0, HUNK_IS_WRITEABLE, 256, 0, 65536*256);
	importList->addSymbol(new Symbol("_ImportList", 0, SYMBOL_IS_RELOCATEABLE, importList));
	for(vector<Hunk*>::iterator it = importHunks.begin(); it != importHunks.end(); it++) {
		Hunk* importHunk = *it;
		unsigned int hashcode = hashCode_1k(importHunk->getImportName());
		importList->addSymbol(new Symbol(importHunk->getName(), hashcode, SYMBOL_IS_RELOCATEABLE, importList));
	}

	HunkList* newHunks = new HunkList;
	Hunk* dllNamesHunk = new Hunk("DllNames", dllnames.c_str(), HUNK_IS_WRITEABLE, 0, dllnames.size(), dllnames.size());
	dllNamesHunk->addSymbol(new Symbol("_DLLNames", 0, SYMBOL_IS_RELOCATEABLE, dllNamesHunk));
	newHunks->addHunkBack(dllNamesHunk);
	newHunks->addHunkBack(importList);

	return newHunks;
}