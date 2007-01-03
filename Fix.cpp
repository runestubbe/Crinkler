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

void FixFile(const char *filename, const char *outfile) {
	MemoryFile file(filename);
	char *data = file.getPtr();
	if (data == 0) {
		Log::error(0, "", "Could not open file '%s'\n", filename);
	}
	int length = file.getSize();

	char majorlv = 0, minorlv = 0;
	if (length >= 200 && *(int *)&data[60] == 12) {
		majorlv = data[38];
		minorlv = data[39];
	}

	if (majorlv < '0' || majorlv > '9' ||
		minorlv < '0' || minorlv > '9') {
		Log::error(0, "", "Input file is not a Crinkler compressed executable");
	}

	if (majorlv == '0' && minorlv == '6') {
		majorlv = '1';
		minorlv = '0';
	}

	printf("File compressed using Crinkler %c.%c\n", majorlv, minorlv);
	switch (majorlv) {
	case '0':
		switch (minorlv) {
		case '1':
		case '2':
			printf("Only files compressed using Crinkler 0.3 or newer can be fixed.\n");
			return;
		case '3':
		case '4':
		case '5':
			break;
		default:
			printf("Unknown Crinkler version.\n");
			return;
		}
		break;
	default:
		printf("No fixing necessary.\n");
		return;
	}

	if (*(short *)&data[106] == 0) {
		printf("File has already been fixed.\n");
		return;
	}

	CoffObjectLoader loader;
	HunkList *hl = loader.load(headerObj, headerObj_end-headerObj, "");
	Symbol *sym = hl->findSymbol("_header");
	char *hptr = sym->hunk->getPtr()+sym->value;

	int models = *(int *)&data[108];
	fix(data, hptr, 2, 1);
	fix(data, hptr, 7, 5);
	fix(data, hptr, 100, 4);
	fix(data, hptr, 106, 11);
	fix(data, hptr, 156, 8);
	*(int *)&data[110] = models;

	if (file.write(outfile)) {
		printf("Fixed file written to '%s'\n", outfile);
	} else {
		Log::error(0, "", "Could not open output file '%s'\n", outfile);
	}
}
