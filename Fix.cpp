#include "MemoryFile.h"
#include "CoffObjectLoader.h"
#include "data.h"
#include "Log.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Symbol.h"

static void fix(char *data, char *hptr, int offset, int size) {
	memcpy(&data[offset], &hptr[offset], size);
}

void FixMemory(char* data) {
	CoffObjectLoader loader;
	HunkList *hl = loader.load(header11Obj, header11Obj_end-header11Obj, "");
	Symbol *sym = hl->findSymbol("_header");
	char *hptr = sym->hunk->getPtr()+sym->value;

	int models = *(int *)&data[108];
	if (*(short *)&data[106] == 0) {
		models = *(int *)&data[110];
	}


	fix(data, hptr, 2, 1);
	fix(data, hptr, 7, 5);
	fix(data, hptr, 54, 1);
	fix(data, hptr, 100, 4);
	fix(data, hptr, 106, 11);
	fix(data, hptr, 156, 8);
	*(int *)&data[110] = models;
}
