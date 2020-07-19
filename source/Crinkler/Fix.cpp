#include <cstring>
#include <initializer_list>

static void Fix(char *data, int offset, std::initializer_list<unsigned char> bytes) {
	memcpy(&data[offset], bytes.begin(), bytes.size());
}

// Fix up old headers just enough to run them:
// - Move entry point to section
// - Clear DLL characteristics

void FixHeader04(char* data) {
	int models = *(int *)&data[0x6C];

	// DepackInit
	Fix(data, 0x02, { 0x68 });       // push (_Unpackeddata)
	Fix(data, 0x07, { 0x31, 0xC0 }); // xor eax, eax
	Fix(data, 0x09, { 0x40 });       // inc eax

	// Entry point
	Fix(data, 0x36, { 0x01 });       // Entry point in 0x410000 header mirror

	// DepackInit2
	Fix(data, 0x64, { 0x31, 0xFF }); // xor edi, edi
	Fix(data, 0x66, { 0x57 });       // push edi
	Fix(data, 0x67, { 0xBB });       // mov ebx, (dword)
	Fix(data, 0x6A, { 0x00, 0x00 }); // DLL characteristics
	Fix(data, 0x6C, { 0x5D });       // pop ebp
	Fix(data, 0x6D, { 0xBE });       // mov esi, (_Models)
	*(int *)&data[0x6E] = models;
	Fix(data, 0x72, { 0x6A, 0x00 }); // push byte 0
	Fix(data, 0x74, { 0x59 });       // pop ecx
}

void FixHeader10(char* data) {
	// Entry point
	Fix(data, 0x36, { 0x01 });       // Entry point in 0x410000 header mirror
}
