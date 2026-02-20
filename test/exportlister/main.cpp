#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE g_stdout = 0;

static void write_buf(const void* buf, DWORD len)
{
	DWORD written = 0;
	WriteFile(g_stdout, buf, len, &written, 0);
}

static void write_str(const char* s)
{
	DWORD len = 0;
	while (s[len]) ++len;
	write_buf(s, len);
}

static void write_hex32(DWORD v)
{
	char out[10];
	out[0] = '0'; out[1] = 'x';
	for (int i = 0; i < 8; ++i)
	{
		int shift = (7 - i) * 4;
		DWORD nib = (v >> shift) & 0xF;
		out[2 + i] = (char)(nib < 10 ? ('0' + nib) : ('A' + (nib - 10)));
	}
	write_buf(out, 10);
}

extern "C" void __stdcall mainCRTStartup()
{
	g_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
	
	BYTE* base = (BYTE*)GetModuleHandleA(0);
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
	IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);

	bool hasExport = false;

	IMAGE_DATA_DIRECTORY expDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (expDir.VirtualAddress && expDir.Size)
	{
		IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(base + expDir.VirtualAddress);

		DWORD* nameRVAs = (DWORD*)(base + exp->AddressOfNames);
		WORD* nameOrds = (WORD*)(base + exp->AddressOfNameOrdinals);
		DWORD* funcRVAs = (DWORD*)(base + exp->AddressOfFunctions);

		DWORD exportStart = expDir.VirtualAddress;
		DWORD exportEnd = expDir.VirtualAddress + expDir.Size;

		for (DWORD i = 0; i < exp->NumberOfNames; ++i)
		{
			const char* name = (const char*)(base + nameRVAs[i]);

			WORD funcIndex = nameOrds[i];
			DWORD rva = funcRVAs[funcIndex];

			write_str(name);
			write_str(" ");

			write_hex32(*(DWORD*)(base + rva));

			write_str("\n");
			hasExport = true;
		}
	}

	if (!hasExport)
	{
		write_str("There are no exports\n");
	}
}
