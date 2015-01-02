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
		Log::error("", "Cannot open DLL '%s'", name);
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

	Log::error("", "Import '%s' cannot be found in '%s'", function, dll);
	return -1;
}

char *getForwardRVA(const char* dll, const char* function) {
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
				return module + address;
			return NULL;
		}
	}

	Log::error("", "Import '%s' cannot be found in '%s'", function, dll);
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


HunkList* ImportHandler::createImportHunks(HunkList* hunklist, Hunk*& hashHunk, map<string, string>& fallbackDlls, const vector<string>& rangeDlls, bool verbose, bool& enableRangeImport) {
	if(verbose)
		printf("\n-Imports----------------------------------\n");

	vector<Hunk*> importHunks;
	vector<bool> usedRangeDlls(rangeDlls.size());
	set<string> usedFallbackDlls;

	//fill list for import hunks
	enableRangeImport = false;
	for(int i = 0; i <hunklist->getNumHunks(); i++) {
		Hunk* hunk = (*hunklist)[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT) {
			do {
				char *forward = getForwardRVA(hunk->getImportDll(), hunk->getImportName());
				if (forward == NULL) break;

				string dllName, functionName;
				int sep = strstr(forward, ".")-forward;
				dllName.append(forward, sep);
				dllName = toLower(dllName);
				functionName.append(&forward[sep+1], strlen(forward)-(sep+1));
				Log::warning("", "Import '%s' from '%s' uses forwarded RVA. Replaced by '%s' from '%s'", 
					hunk->getImportName(), hunk->getImportDll(), functionName.c_str(), dllName.c_str());
				hunk = new Hunk(hunk->getName(), functionName.c_str(), dllName.c_str());
			} while (true);

			//is the dll a range dll?
			for(int i = 0; i < (int)rangeDlls.size(); i++) {
				if(toUpper(rangeDlls[i]) == toUpper(hunk->getImportDll())) {
					usedRangeDlls[i] = true;
					enableRangeImport = true;
					break;
				}
			}
			importHunks.push_back(hunk);
		}
	}

	//sort import hunks
	sort(importHunks.begin(), importHunks.end(), importHunkRelation);

	vector<unsigned int> hashes;
	Hunk* importList = new Hunk("ImportListHunk", 0, HUNK_IS_WRITEABLE, 16, 0, 0);
	char dllNames[1024] = {0};
	char* dllNamesPtr = dllNames+1;
	char* hashCounter = dllNames;
	string currentDllName;
	int pos = 0;
	for(vector<Hunk*>::const_iterator it = importHunks.begin(); it != importHunks.end();) {
		Hunk* importHunk = *it;
		bool useRange = false;

		//is the dll a range dll?
		for(int i = 0; i < (int)rangeDlls.size(); i++) {
			if(toUpper(rangeDlls[i]) == toUpper(importHunk->getImportDll())) {
				usedRangeDlls[i] = true;
				useRange = true;
				break;
			}
		}

		//skip non hashes

		if(currentDllName.compare(importHunk->getImportDll())) {
			if(strcmp(importHunk->getImportDll(), "kernel32") != 0) {
				set<string> seen;
				string dll = importHunk->getImportDll();
				strcpy_s(dllNamesPtr, sizeof(dllNames)-(dllNamesPtr-dllNames), dll.c_str());
				dllNamesPtr += dll.size() + 1;
				while (fallbackDlls.count(dll) != 0) {
					usedFallbackDlls.insert(dll);
					seen.insert(dll);
					*dllNamesPtr = 0;
					dllNamesPtr += 1;
					dll = fallbackDlls[dll];
					strcpy_s(dllNamesPtr, sizeof(dllNames) - (dllNamesPtr - dllNames), dll.c_str());
					dllNamesPtr += dll.size() + 1;
					if (seen.count(dll) != 0) Log::error("", "Cyclic DLL fallback");
				}
				hashCounter = dllNamesPtr;
				*hashCounter = 0;
				dllNamesPtr += 1;
			}


			currentDllName = importHunk->getImportDll();
			if(verbose)
				printf("%s\n", currentDllName.c_str());
		}

		(*hashCounter)++;
		int hashcode = hashCode(importHunk->getImportName());
		hashes.push_back(hashcode);
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

	//warn about unused range dlls
	for (int i = 0; i < (int)rangeDlls.size(); i++) {
		if (!usedRangeDlls[i]) {
			Log::warning("", "No functions were imported from range DLL '%s'", rangeDlls[i].c_str());
		}
	}

	//warn about unused fallback dlls
	for (auto fallback : fallbackDlls) {
		if (usedFallbackDlls.count(fallback.first) == 0) {
			Log::warning("", "No functions were imported from fallback DLL '%s'", fallback.first.c_str());
		}
	}

	importList->setVirtualSize(pos*4);
	importList->addSymbol(new Symbol("_ImportList", 0, SYMBOL_IS_RELOCATEABLE, importList));
	importList->addSymbol(new Symbol(".bss", 0, SYMBOL_IS_RELOCATEABLE|SYMBOL_IS_SECTION, importList, "crinkler import"));

	hashHunk = new Hunk("HashHunk", (char*)&hashes[0], 0, 0, hashes.size()*sizeof(unsigned int), hashes.size()*sizeof(unsigned int));
	
	//create new hunklist
	HunkList* newHunks = new HunkList;

	newHunks->addHunkBack(importList);

	Hunk* dllNamesHunk = new Hunk("DllNames", dllNames, HUNK_IS_WRITEABLE, 0, dllNamesPtr - dllNames, dllNamesPtr - dllNames);
	dllNamesHunk->addSymbol(new Symbol(".data", 0, SYMBOL_IS_RELOCATEABLE|SYMBOL_IS_SECTION, dllNamesHunk, "crinkler import"));
	dllNamesHunk->addSymbol(new Symbol("_DLLNames", 0, SYMBOL_IS_RELOCATEABLE, dllNamesHunk));
	newHunks->addHunkBack(dllNamesHunk);

	return newHunks;
}

const unsigned int hashCode_1k(const char* str, int family, int bits) {
	int eax = 0;
	char al = 0;
	do {
		eax = (eax & 0xFFFFFF00) + *str++;
		eax = eax * family;
		al = eax & 0xFF;
		al = al+al;
		eax = (eax & 0xFFFFFF00) | (unsigned char)al;
	} while(al!=0);
	//code ^= _rotl(code, 16);
	unsigned int ueax = (unsigned int)eax;
	return (ueax >> (32-bits))*4;
}

static void findCollisionFreeHashFamily(const set<string>& dlls, const vector<Hunk*>& importHunks, int& hash_family, int& hash_bits) {
	vector<char*> modules;
	for(set<string>::const_iterator it = dlls.begin(); it != dlls.end(); it++) {
		modules.push_back(LoadDLL(it->c_str()));
	}

	int stime = GetTickCount();
	cout << "searching for hash family: " << endl;
	int best_family = 0;
	int best_num_bits = 0;
	int next_family = 1;
	for(int num_bits = 20; num_bits >= 1; num_bits--)
	{
		bool has_collisions = true;
		int family;
		int digit_mul = 1;
		for(int num_digits = 1; num_digits <= 3 && has_collisions; num_digits++)
		{
			for(int digit = 1; digit < 256 && has_collisions; digit++)
			{
				map<int, int> buckets;
				family = digit*digit_mul*256 + 1;
				has_collisions = false;
			
				for(vector<char*>::const_iterator it = modules.begin(); it != modules.end(); it++) {
					char* module = *it;

					IMAGE_DOS_HEADER* dh = (IMAGE_DOS_HEADER*)module;
					IMAGE_FILE_HEADER* coffHeader = (IMAGE_FILE_HEADER*)(module + dh->e_lfanew+4);
					IMAGE_OPTIONAL_HEADER32* pe = (IMAGE_OPTIONAL_HEADER32*)(coffHeader+1);
					IMAGE_EXPORT_DIRECTORY* exportdir = (IMAGE_EXPORT_DIRECTORY*) (module + pe->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

					int* nameTable = (int*)(module + exportdir->AddressOfNames);
					for(int i = 0; i < (int)exportdir->NumberOfNames; i++) {
						char* name = module+nameTable[i];
						int hash = hashCode_1k(name, family, num_bits);
						buckets[hash]++;
					}
				}

				for(vector<Hunk*>::const_iterator it = importHunks.begin(); it != importHunks.end(); it++) {
					string name = (*it)->getImportName();
					int hashcode = hashCode_1k(name.c_str(), family, num_bits);
					
					if(buckets[hashcode] > 1 && name != "timeGetTime") {
						has_collisions = true;
						break;
					}
				}
			}
			digit_mul = (digit_mul<<8)+1;
		}

		if(has_collisions)
		{
			break;
		}
		else
		{
			printf("%dbits: %8x\n", num_bits, family); fflush(stdout);
			best_family = family;
			best_num_bits = num_bits;
		}
	}
	hash_family = best_family;
	hash_bits = best_num_bits;
	printf("time spent: %dms\n", GetTickCount()-stime);
	printf("done looking for hash family\n"); fflush(stdout);
}

HunkList* ImportHandler::createImportHunks1K(HunkList* hunklist, bool verbose, int& hash_bits, int& max_dll_name_length) {
	if(verbose)
		printf("\n-Imports----------------------------------\n");

	vector<Hunk*> importHunks;
	set<string> dlls;

	//fill list for import hunks
	for(int i = 0; i <hunklist->getNumHunks(); i++) {
		Hunk* hunk = (*hunklist)[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT) {
			dlls.insert(hunk->getImportDll());
			if(getForwardRVA(hunk->getImportDll(), hunk->getImportName()) != NULL) {
				Log::error("", "Import '%s' from '%s' uses forwarded RVA. This feature is not supported by crinkler (yet)", 
					hunk->getImportName(), hunk->getImportDll());
			}
			importHunks.push_back(hunk);
		}
	}
	int hash_family;
	findCollisionFreeHashFamily(dlls, importHunks, hash_family, hash_bits);

	string dllnames;
	
	int max_name_length = 0;
	for(string dllname : dlls)
	{
		max_name_length = max(max_name_length, (int)dllname.size() + 1);
	}

	for(set<string>::iterator it = dlls.begin(); it != dlls.end(); it++)
	{
		while(dllnames.size() % max_name_length)
			dllnames.push_back(0);
		if(it->compare("kernel32"))
		{
			string name = *it;
			dllnames += name;
		}
	}

	Hunk* importList = new Hunk("ImportListHunk", 0, HUNK_IS_WRITEABLE, 8, 0, 65536*256);
	importList->addSymbol(new Symbol("_HashFamily", hash_family, 0, importList));
	importList->addSymbol(new Symbol("_ImportList", 0, SYMBOL_IS_RELOCATEABLE, importList));
	for(vector<Hunk*>::iterator it = importHunks.begin(); it != importHunks.end(); it++)
	{
		Hunk* importHunk = *it;
		unsigned int hashcode = hashCode_1k(importHunk->getImportName(), hash_family, hash_bits);
		importList->addSymbol(new Symbol(importHunk->getName(), hashcode, SYMBOL_IS_RELOCATEABLE, importList));
	}

	HunkList* newHunks = new HunkList;
	Hunk* dllNamesHunk = new Hunk("DllNames", dllnames.c_str(), HUNK_IS_WRITEABLE, 0, dllnames.size()+1, dllnames.size()+1);
	dllNamesHunk->addSymbol(new Symbol("_DLLNames", 0, SYMBOL_IS_RELOCATEABLE, dllNamesHunk));
	newHunks->addHunkBack(dllNamesHunk);
	newHunks->addHunkBack(importList);
	max_dll_name_length = max_name_length;

	return newHunks;
}