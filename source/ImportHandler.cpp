#include <windows.h>
#include <fstream>
#include "ImportHandler.h"

#include "HunkList.h"
#include "Hunk.h"
#include "StringMisc.h"
#include "Log.h"
#include "Symbol.h"
#include "data.h"

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <ppl.h>
#include <assert.h>

using namespace std;

const char *LoadDLL(const char *name);

unsigned int RVAToFileOffset(const char* module, unsigned int rva)
{
	const IMAGE_DOS_HEADER* pDH = (const PIMAGE_DOS_HEADER)module;
	const IMAGE_NT_HEADERS32* pNTH = (const PIMAGE_NT_HEADERS32)(module + pDH->e_lfanew);
	int numSections = pNTH->FileHeader.NumberOfSections;
	int numDataDirectories = pNTH->OptionalHeader.NumberOfRvaAndSizes;
	const IMAGE_SECTION_HEADER* sectionHeaders = (const IMAGE_SECTION_HEADER*)&pNTH->OptionalHeader.DataDirectory[numDataDirectories];
	for(int i = 0; i < numSections; i++)
	{
		if(rva >= sectionHeaders[i].VirtualAddress && rva < sectionHeaders[i].VirtualAddress + sectionHeaders[i].SizeOfRawData)
		{
			return rva - sectionHeaders[i].VirtualAddress + sectionHeaders[i].PointerToRawData;
		}
	}
	return rva;
}

int getOrdinal(const char* function, const char* dll) {
	const char* module = LoadDLL(dll);

	const IMAGE_DOS_HEADER* dh = (const IMAGE_DOS_HEADER*)module;
	const IMAGE_FILE_HEADER* coffHeader = (const IMAGE_FILE_HEADER*)(module + dh->e_lfanew + 4);
	const IMAGE_OPTIONAL_HEADER32* pe = (const IMAGE_OPTIONAL_HEADER32*)(coffHeader + 1);
	const IMAGE_EXPORT_DIRECTORY* exportdir = (const IMAGE_EXPORT_DIRECTORY*) (module + RVAToFileOffset(module, pe->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress));
	
	const short* ordinalTable = (const short*) (module + RVAToFileOffset(module, exportdir->AddressOfNameOrdinals));
	const int* nameTable = (const int*)(module + RVAToFileOffset(module, exportdir->AddressOfNames));
	for(int i = 0; i < (int)exportdir->NumberOfNames; i++) {
		int ordinal = ordinalTable[i] + exportdir->Base;
		const char* name = module + RVAToFileOffset(module, nameTable[i]);
		if(strcmp(name, function) == 0) {
			return ordinal;
		}
	}

	Log::error("", "Import '%s' cannot be found in '%s'", function, dll);
	return -1;
}


const char *getForwardRVA(const char* dll, const char* function) {
	const char* module = LoadDLL(dll);
	const IMAGE_DOS_HEADER* pDH = (const PIMAGE_DOS_HEADER)module;
	const IMAGE_NT_HEADERS32* pNTH = (const PIMAGE_NT_HEADERS32)(module + pDH->e_lfanew);

	const IMAGE_EXPORT_DIRECTORY* pIED = (const PIMAGE_EXPORT_DIRECTORY)(module + RVAToFileOffset(module, pNTH->OptionalHeader.DataDirectory[0].VirtualAddress));

	const short* ordinalTable = (const short*)(module + RVAToFileOffset(module, pIED->AddressOfNameOrdinals));
	const DWORD* namePointerTable = (const DWORD*)(module + RVAToFileOffset(module, pIED->AddressOfNames));
	const DWORD* addressTableRVAOffset = addressTableRVAOffset = (const DWORD*)(module + RVAToFileOffset(module, pIED->AddressOfFunctions));

	for(unsigned int i = 0; i < pIED->NumberOfNames; i++) {
		short ordinal = ordinalTable[i];
		const char* name = (const char*)(module + RVAToFileOffset(module, namePointerTable[i]));

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


Hunk* forwardImport(Hunk* hunk) {
	do {
		const char *forward = getForwardRVA(hunk->getImportDll(), hunk->getImportName());
		if (forward == NULL) break;

		string dllName, functionName;
		int sep = int(strstr(forward, ".") - forward);
		dllName.append(forward, sep);
		dllName = toLower(dllName);
		functionName.append(&forward[sep + 1], strlen(forward) - (sep + 1));
		Log::warning("", "Import '%s' from '%s' uses forwarded RVA. Replaced by '%s' from '%s'",
			hunk->getImportName(), hunk->getImportDll(), functionName.c_str(), dllName.c_str());
		hunk = new Hunk(hunk->getName(), functionName.c_str(), dllName.c_str());
	} while (true);
	return hunk;
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
			hunk = forwardImport(hunk);

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
		if(currentDllName.compare(importHunk->getImportDll()))
		{
			if(strcmp(importHunk->getImportDll(), "kernel32") != 0)
			{
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

		while(useRange && it != importHunks.end() && currentDllName.compare((*it)->getImportDll()) == 0)	// import the rest of the range
		{
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

	hashHunk = new Hunk("HashHunk", (char*)&hashes[0], 0, 0, int(hashes.size()*sizeof(unsigned int)), int(hashes.size()*sizeof(unsigned int)));
	
	//create new hunklist
	HunkList* newHunks = new HunkList;

	newHunks->addHunkBack(importList);

	Hunk* dllNamesHunk = new Hunk("DllNames", dllNames, HUNK_IS_WRITEABLE | HUNK_IS_LEADING, 0, int(dllNamesPtr - dllNames), int(dllNamesPtr - dllNames));
	dllNamesHunk->addSymbol(new Symbol(".data", 0, SYMBOL_IS_RELOCATEABLE|SYMBOL_IS_SECTION, dllNamesHunk, "crinkler import"));
	dllNamesHunk->addSymbol(new Symbol("_DLLNames", 0, SYMBOL_IS_RELOCATEABLE, dllNamesHunk));
	newHunks->addHunkBack(dllNamesHunk);

	return newHunks;
}

__forceinline unsigned int hashCode_1k(const char* str, int hash_multiplier, int hash_bits)
{
	int eax = 0;
	unsigned char c;
	do
	{
		c = *str++;
		eax = ((eax & 0xFFFFFF00) + c) * hash_multiplier;
	} while(c & 0x7F);

	eax = (eax & 0xFFFFFF00) | (unsigned char)(c + c);

	return ((unsigned int)eax) >> (32 - hash_bits);
}

static bool solve_dll_order_constraints(std::vector<unsigned int>& constraints, unsigned int* new_order)
{
	if(constraints[0] > 1)	// kernel32 must be first. it can't have dependencies on anything else
	{
		return false;
	}

	std::vector<unsigned int> constraints2 = constraints;
	unsigned int used_mask = 0;

	int num = (int)constraints.size();
	for(int i = 0; i < num; i++)
	{
		int selected = -1;
		for(int j = 0; j < num; j++)
		{
			if(((used_mask >> j) & 1) == 0 && (constraints[j] == 0))
			{
				selected = j;
				break;
			}
		}

		if(selected == -1)
		{
			return false;
		}

		
		*new_order++ = selected;
		used_mask |= (1u<<selected);
		for(int j = 0; j < num; j++)
		{
			constraints[j] &= ~(1u<<selected);
		}
	}

	return true;
}

static void AddKnownExportsForDll(std::vector<string>& exports, const char* dll_name)
{
	struct s_known_exports_header
	{
		int num_dlls;
		struct
		{
			int name_offset;
			int num_exports;
			int export_name_offset_table;
		} dll_infos[1];
	};

	const s_known_exports_header* known_exports_header = (const s_known_exports_header*)knownDllExports;
	
	int num_known_dlls = known_exports_header->num_dlls;
	for(int known_dll_index = 0; known_dll_index < num_known_dlls; known_dll_index++)
	{
		const char* known_dll_name = knownDllExports + known_exports_header->dll_infos[known_dll_index].name_offset;
		if(strcmp(dll_name, known_dll_name) == 0)
		{
			int num_exports = known_exports_header->dll_infos[known_dll_index].num_exports;
			const int* offset_table = (const int*) ((const char*)knownDllExports + known_exports_header->dll_infos[known_dll_index].export_name_offset_table);
			for(int i = 0; i < num_exports; i++)
			{
				const char* name = knownDllExports + offset_table[i];
				exports.push_back(name);
			}
			break;
		}
	}
}

static bool findCollisionFreeHash(vector<string>& dll_names, const vector<Hunk*>& importHunks, int& hash_multiplier, int& hash_bits)
{
	//printf("searching for hash function:\n"); fflush(stdout);

	assert(dll_names.size() <= 32);

	dll_names.erase(std::find(dll_names.begin(), dll_names.end(), string("kernel32")));
	dll_names.insert(dll_names.begin(), "kernel32");
	
	struct SDllInfo
	{
		std::vector<std::string>	exports;
		std::vector<char>			used;
	};

	int num_dlls = (int)dll_names.size();
	std::vector<unsigned int> best_dll_order(num_dlls);

	// load dlls and mark functions that are imported
	vector<SDllInfo> dllinfos(num_dlls);
	
	for(int dll_index = 0; dll_index < num_dlls; dll_index++)
	{
		const char* dllname = dll_names[dll_index].c_str();
		SDllInfo& info = dllinfos[dll_index];

		{
			// scrape exports from dll on this machine
			const char* module = LoadDLL(dllname);

			const IMAGE_DOS_HEADER* dh = (const IMAGE_DOS_HEADER*)module;
			const IMAGE_FILE_HEADER* coffHeader = (const IMAGE_FILE_HEADER*)(module + dh->e_lfanew + 4);
			const IMAGE_OPTIONAL_HEADER32* pe = (const IMAGE_OPTIONAL_HEADER32*)(coffHeader + 1);
			const IMAGE_EXPORT_DIRECTORY* exportdir = (const IMAGE_EXPORT_DIRECTORY*)(module + RVAToFileOffset(module, pe->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress));
			int num_names = exportdir->NumberOfNames;
			const int* name_table = (const int*)(module + RVAToFileOffset(module, exportdir->AddressOfNames));
			for(int i = 0; i < num_names; i++)
			{
				const char* name = module + RVAToFileOffset(module, name_table[i]);
				info.exports.push_back(name);
			}
		}

		// combine with list of known exports for this dll
		AddKnownExportsForDll(info.exports, dllname);
		std::sort(info.exports.begin(), info.exports.end());
		info.exports.erase(std::unique(info.exports.begin(), info.exports.end()), info.exports.end());

		int num_exports = (int)info.exports.size();
		info.used.resize(num_exports);

		for(Hunk* importHunk : importHunks)
		{
			if(strcmp(dllname, importHunk->getImportDll()) == 0)
			{
				// mark those that are used
				auto it = std::find(info.exports.begin(), info.exports.end(), importHunk->getImportName());
				
				if(it != info.exports.end())
				{
					int idx = (int)std::distance(info.exports.begin(), it);
					info.used[idx] = 1;
				}
				else
				{
					assert(false);
					Log::error("", "Could not find '%s' in '%s'", importHunk->getImportName(), importHunk->getImportDll());
				}
			}
		}
	}

	const int MAX_BITS = 16;

	int best_num_bits = INT_MAX;
	
	// Find hash function that works
	// We do however allow hash overlaps from separate dlls.
	// To exploit this we sort the dlls to avoid collisions when possible
	
	struct SBucket
	{
		unsigned int	unreferenced_functions_dll_mask;
		unsigned char	referenced_function_dll_index;		//dll_index + 1
	};
	int best_low_byte = INT_MAX;
	int best_high_byte = INT_MAX;
	
	concurrency::critical_section cs;
	for(int num_bits = MAX_BITS; num_bits >= 1; num_bits--)
	{
		concurrency::parallel_for(0, 256, [&](int high_byte)	//TODO: don't start from 0
		{
			{
				Concurrency::critical_section::scoped_lock l(cs);
				if(num_bits == best_num_bits && high_byte > best_high_byte)
				{
					return;
				}
			}
			std::vector<unsigned int> dll_constraints(num_dlls);
			std::vector<unsigned int> new_dll_order(num_dlls);
			SBucket* buckets = new SBucket[(size_t)1 << num_bits];
			for(int low_byte = 0; low_byte < 256; low_byte++)
			{
				for(int dll_index = 0; dll_index < num_dlls; dll_index++)
				{
					dll_constraints[dll_index] = 0;
				}

				int hash_multiplier = (high_byte << 16) | (low_byte << 8) | 1;

				memset(buckets, 0, sizeof(SBucket) << num_bits);
				bool has_collisions = false;

				unsigned int dll_index = 0;
				for(SDllInfo& dllinfo : dllinfos)
				{
					unsigned int dll_mask = (1u << dll_index);
					
					int num_names = (int)dllinfo.exports.size();
					for(int i = 0; i < num_names; i++)
					{
						unsigned int hashcode = hashCode_1k(dllinfo.exports[i].c_str(), hash_multiplier, num_bits);
						bool new_referenced = dllinfo.used[i];
						bool old_referenced = buckets[hashcode].referenced_function_dll_index > 0;

						if(new_referenced)
						{
							if(old_referenced)
							{
								has_collisions = true;
								break;
							}
							else
							{
								buckets[hashcode].referenced_function_dll_index = dll_index + 1;
								buckets[hashcode].unreferenced_functions_dll_mask &= ~dll_mask;	// clear unreferenced before this

								dll_constraints[dll_index] |= buckets[hashcode].unreferenced_functions_dll_mask;
							}
						}
						else
						{
							buckets[hashcode].unreferenced_functions_dll_mask |= dll_mask;
							if(old_referenced)
							{
								int old_dll_index = buckets[hashcode].referenced_function_dll_index - 1;
								if(old_dll_index == dll_index)
								{
									has_collisions = true;
									break;
								}
								dll_constraints[old_dll_index] |= dll_mask;
							}
						}
					}
					dll_index++;

					if(has_collisions)
					{
						break;
					}
				}

				if(!has_collisions && solve_dll_order_constraints(dll_constraints, &new_dll_order[0]))
				{
					Concurrency::critical_section::scoped_lock l(cs);
					if(num_bits < best_num_bits || high_byte < best_high_byte)
					{
						best_low_byte = low_byte;
						best_high_byte = high_byte;
						best_num_bits = num_bits;
						best_dll_order = new_dll_order;
					}
					break;
				}
			}

			delete[] buckets;
		});

		int best_hash_multiplier = (best_high_byte << 16) | (best_low_byte << 8) | 1;
		if(best_num_bits > num_bits)
		{
			break;
		}
	}
	int best_hash_multiplier = (best_high_byte << 16) | (best_low_byte << 8) | 1;

	if(best_num_bits == INT_MAX)
	{
		return false;
	}

	// reorder dlls
	std::vector<std::string> new_dlls(num_dlls);
	for(int i = 0; i < num_dlls; i++)
	{
		new_dlls[i] = dll_names[best_dll_order[i]];
	}
	dll_names = new_dlls;
	
	hash_multiplier = best_hash_multiplier;
	hash_bits = best_num_bits;
	return true;
}

HunkList* ImportHandler::createImportHunks1K(HunkList* hunklist, bool verbose, int& hash_bits, int& max_dll_name_length) {
	if (verbose)
	{
		printf("\n-Imports----------------------------------\n");
	}

	vector<Hunk*> importHunks;
	set<string> dll_set;

	bool found_kernel32 = false;

	//fill list for import hunks
	for(int i = 0; i < hunklist->getNumHunks(); i++)
	{
		Hunk* hunk = (*hunklist)[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT)
		{
			hunk = forwardImport(hunk);
			if(strcmp(hunk->getImportDll(), "kernel32") == 0)
			{
				found_kernel32 = true;
			}
			dll_set.insert(hunk->getImportDll());
			importHunks.push_back(hunk);
		}
	}

	if(!found_kernel32)
	{
		Log::error("", "Kernel32 needs to be linked for import code to function.");
	}

	int hash_multiplier;
	vector<string> dlls(dll_set.begin(), dll_set.end());
	if (!findCollisionFreeHash(dlls, importHunks, hash_multiplier, hash_bits))
	{
		Log::error("", "Could not find collision-free hash function");
	}

	string dllnames;
	
	int max_name_length = 0;
	for(string name : dlls)
	{
		max_name_length = max(max_name_length, (int)name.size() + 1);
	}

	for(string name : dlls)
	{
		while (dllnames.size() % max_name_length)
		{
			dllnames.push_back(0);
		}
		
		if(name.compare("kernel32") != 0)
		{
			dllnames += name;
		}
	}
	 
	Hunk* importList = new Hunk("ImportListHunk", 0, HUNK_IS_WRITEABLE, 8, 0, 65536*256);
	importList->addSymbol(new Symbol("_HashMultiplier", hash_multiplier, 0, importList));
	importList->addSymbol(new Symbol("_ImportList", 0, SYMBOL_IS_RELOCATEABLE, importList));
	for(Hunk* importHunk : importHunks)
	{
		unsigned int hashcode = hashCode_1k(importHunk->getImportName(), hash_multiplier, hash_bits);
		importList->addSymbol(new Symbol(importHunk->getName(), hashcode*4, SYMBOL_IS_RELOCATEABLE, importList));
	}

	if(verbose)
	{
		for(string dllname : dlls)
		{
			printf("%s\n", dllname.c_str());
				
			for(Hunk* importHunk : importHunks)
			{
				if(strcmp(importHunk->getImportDll(), dllname.c_str()) == 0)
				{
					int ordinal = getOrdinal(importHunk->getImportName(), importHunk->getImportDll());
					printf("  %s (ordinal %d)\n", importHunk->getImportName(), ordinal);
				}
			}
		}
	}

	HunkList* newHunks = new HunkList;
	Hunk* dllNamesHunk = new Hunk("DllNames", dllnames.c_str(), HUNK_IS_WRITEABLE | HUNK_IS_LEADING, 0, (int)dllnames.size() + 1, (int)dllnames.size() + 1);
	dllNamesHunk->addSymbol(new Symbol("_DLLNames", 0, SYMBOL_IS_RELOCATEABLE, dllNamesHunk));
	newHunks->addHunkBack(dllNamesHunk);
	newHunks->addHunkBack(importList);
	max_dll_name_length = max_name_length;

	printf(
		"\n"
		"Note: Programs linked using the TINYIMPORT option may break if a future Windows\n"
		"version adds functions to one of the imported DLLs. Such breakage cannot be\n"
		"fixed by using the RECOMPRESS feature. When using this option, it is strongly\n"
		"recommended to also distribute a version of your program linked using the\n"
		"normal import mechanism (without the TINYIMPORT option).\n"
	);

	return newHunks;
}