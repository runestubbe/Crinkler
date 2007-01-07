#include "CallTransform.h"
#include "Hunk.h"
#include "CoffObjectLoader.h"
#include "data.h"
#include "HunkList.h"
#include "Symbol.h"

Hunk* CallTransform::getDetransformer() {
	CoffObjectLoader loader;
	HunkList* hl = loader.load(calltransObj, calltransObj_end - calltransObj, "call detransform");
	Hunk* h = hl->toHunk("call detransformer");
	delete hl;
	return h;
}

void CallTransform::transform(Hunk* hunk, int splittingPoint) {
	unsigned char* data = (unsigned char*)hunk->getPtr();
	int size = splittingPoint;

	int num = 0;
	for (int i = 0 ; i < size-4 ; i++) {
		if (data[i] == 0xe8) {
			int *offset = (int *)&data[i+1];
			if (*offset >= -32768 && *offset <= 32767) {
				*offset = (int)(short)(*offset + i+1);
				i += 4;
				num++;
				if (num == 255) break;
			}
		}
	}
	*(hunk->getPtr() + hunk->findSymbol("_NumCallTransPtr")->value) = num;
	printf("\nCalls transformed: %d\n", num);
}