#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <set>
#include <vector>
#include <iterator>
#include <cassert>

#define ROOT_DIRECTORY		"dlls"
#define OUTPUT_PATH			"..\\known_dll_exports.dat"

// file format:
//   num_dlls
//   num_dlls x dll_info_header
//   offset table
//   string pool

struct dll_info_header
{
	int name_offset;
	int num_exports;
	int export_name_offset_table;
};

static void ScrapeFile(std::set<std::string>& symbols, const char* filename)
{
	HMODULE module = LoadLibraryEx(filename, NULL, DONT_RESOLVE_DLL_REFERENCES);
	unsigned char* data = (unsigned char*)module;

	IMAGE_DOS_HEADER* dh = (IMAGE_DOS_HEADER*)data;
	IMAGE_FILE_HEADER* coffHeader = (IMAGE_FILE_HEADER*)(data + dh->e_lfanew + 4);
	IMAGE_OPTIONAL_HEADER32* pe = (IMAGE_OPTIONAL_HEADER32*)(coffHeader + 1);
	IMAGE_EXPORT_DIRECTORY* exportdir = (IMAGE_EXPORT_DIRECTORY*)(data + pe->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
	int num_names = exportdir->NumberOfNames;
	int* nameTable = (int*)(data + exportdir->AddressOfNames);

	for (int i = 0; i < num_names; i++)
	{
		char* name = (char*)(data + nameTable[i]);
		symbols.insert(name);
	}

	printf("  %s: %d\n", filename, num_names);

	FreeLibrary(module);
}

static void ScrapeDirectory(std::set<std::string>& symbols, const char* dlldir)
{
	printf("scraping directory: %s\n", dlldir);
	WIN32_FIND_DATA ffd;
	char search_str[MAX_PATH];
	sprintf(search_str, "%s\\%s\\*", ROOT_DIRECTORY, dlldir);
	
	HANDLE hFind = FindFirstFile(search_str, &ffd);
	assert(hFind != INVALID_HANDLE_VALUE);
	do
	{
		if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			char filename[MAX_PATH];
			sprintf(filename, "%s\\%s\\%s", ROOT_DIRECTORY, dlldir, ffd.cFileName);
			ScrapeFile(symbols, filename);
		}
	} while (FindNextFile(hFind, &ffd) != 0);

	printf("  total: %d\n", symbols.size());
}

int main(int argc, const char* argv[])
{
	std::vector<dll_info_header> headers;
	std::vector<int> symbol_table;
	std::vector<char> string_pool;
	
	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFile(ROOT_DIRECTORY"\\*", &ffd);
	assert(hFind != INVALID_HANDLE_VALUE);
	do
	{
		if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(ffd.cFileName, ".") && strcmp(ffd.cFileName, ".."))
		{
			std::set<std::string> symbols;
			ScrapeDirectory(symbols, ffd.cFileName);
			
			dll_info_header header;
			header.name_offset = string_pool.size();
			header.num_exports = symbols.size();
			header.export_name_offset_table = symbol_table.size() * sizeof(int);
			headers.push_back(header);

			// add filename to pool
			std::string filename = ffd.cFileName;
			std::copy(filename.begin(), filename.end(), back_inserter(string_pool));
			string_pool.push_back(0);

			// add dll names to pool
			for (std::string str : symbols)
			{
				symbol_table.push_back(string_pool.size());
				std::copy(str.begin(), str.end(), back_inserter(string_pool));
				string_pool.push_back(0);
			}
		}
	} while (FindNextFile(hFind, &ffd) != 0);


	int num_headers = headers.size();
	int num_offsets = symbol_table.size();

	int name_offset_table_start = sizeof(int) + num_headers * sizeof(dll_info_header);
	int string_pool_start = name_offset_table_start + num_offsets * sizeof(int);

	for (dll_info_header& header : headers)
	{
		header.name_offset += string_pool_start;
		header.export_name_offset_table += name_offset_table_start;
	}

	for (int& offset : symbol_table)
	{
		offset += string_pool_start;
	}

	FILE* outfile = fopen(OUTPUT_PATH, "wb");
	assert(outfile);
	fwrite(&num_headers, 4, 1, outfile);
	fwrite(headers.data(), num_headers * sizeof(dll_info_header), 1, outfile);
	fwrite(symbol_table.data(), symbol_table.size() * sizeof(int), 1, outfile);
	fwrite(string_pool.data(), string_pool.size(), 1, outfile);
	fclose(outfile);

	return 0;
}