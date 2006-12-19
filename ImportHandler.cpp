#include <windows.h>
#include "ImportHandler.h"

#include "HunkList.h"
#include "Hunk.h"
#include "StringMisc.h"
#include "Log.h"

#include <algorithm>
#include <vector>

using namespace std;

int getOrdinal(const char* function, const char* dll) {
	char* module = (char*)LoadLibrary(dll);
	if(module == 0)
		return -1;

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

	//TODO: should I free the module?
	return -1;
}

bool isForwardRVA(const char* dll, const char* function) {
	char* module = (char*)LoadLibrary(dll);
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

	Log::error(0, "", "import %s could not be found in %s", function, dll);

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

//TODO: report unused range dlls
HunkList* ImportHandler::createImportHunks(HunkList* hunklist, Hunk* hashHunk, const list<string>& rangeDlls, bool verbose) {
	if(verbose)
		printf("\n-Imports----------------------------------\n");


	vector<Hunk*> importHunks;
	for(int i = 0; i <hunklist->getNumHunks(); i++) {
		Hunk* hunk = (*hunklist)[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT) {
			if(isForwardRVA(hunk->getImportDll(), hunk->getImportName())) {
				Log::error(0, "", "import %s from %s uses forwarded RVA. This feature is not supported by crinkler (yet)", 
					hunk->getImportName(), hunk->getImportDll());
			}
			importHunks.push_back(hunk);
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
		for(list<string>::const_iterator jt = rangeDlls.begin(); jt != rangeDlls.end(); jt++) {
			if(toUpper(*jt) == toUpper(importHunk->getImportDll())) {
				useRange = true;
				break;
			}
		}

		
		//skip non hashes
		while(*hashptr != 0x48534148) {
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

			if(verbose) {
				printf("    %s (ordinal %d)\n", (*it)->getImportName(), startOrdinal);		
			}

			if(o - startOrdinal >= 254)
				break;

			ordinal = o;
			importList->addSymbol(new Symbol((*it)->getName(), (pos+ordinal-startOrdinal)*4, SYMBOL_IS_RELOCATEABLE, importList));
			it++;
		}

		if(verbose && useRange)
			printf("  }\n");

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