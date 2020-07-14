#include "MemoryFile.h"
#include "CoffObjectLoader.h"
#include "data.h"
#include "Log.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Symbol.h"

static void Fix(char *data, char *hptr, int offset, int size) {
	memcpy(&data[offset], &hptr[offset], size);
}

void FixMemory(char* data) {
	CoffObjectLoader loader;
	HunkList *hl = loader.Load(header11Obj, int(header11Obj_end-header11Obj), "");
	Symbol *sym = hl->FindSymbol("_header");
	char *hptr = sym->hunk->GetPtr()+sym->value;

	int models = *(int *)&data[108];
	if (*(short *)&data[106] == 0) {
		models = *(int *)&data[110];
	}

	Fix(data, hptr, 2, 1);
	Fix(data, hptr, 7, 5);
	Fix(data, hptr, 54, 1);
	Fix(data, hptr, 100, 4);
	Fix(data, hptr, 106, 11);
	Fix(data, hptr, 156, 8);
	*(int *)&data[110] = models;
}
