#include "CallTransform.h"
#include "Hunk.h"
#include "CoffObjectLoader.h"
#include "data.h"
#include "PartList.h"
#include "Symbol.h"
#include "Log.h"

Hunk* CallTransform::GetDetransformer() {
	CoffObjectLoader loader;
	PartList partlist;
	loader.Load(partlist.GetCodePart(), calltransObj, int(calltransObj_end - calltransObj), "call detransform");
	return partlist.Link("call detransformer", 0);
}

bool CallTransform::DoTransform(Hunk* hunk, int codeSize, bool verbose) {
	unsigned char* data = (unsigned char*)hunk->GetPtr();
	
	int num = 0;
	for (int i = 0 ; i < codeSize-4 ; i++) {
		if (data[i] == 0xe8) {
			int *offset = (int *)&data[i+1];
			if (*offset >= -32768 && *offset <= 32767) {
				*offset = (int)(short)(*offset + i+1);
				i += 4;
				num++;
			}
		}
	}
	if (num > 0) {
		*(int *)(hunk->GetPtr() + hunk->FindSymbol("_CallTrans")->value+2) = num;
		if(verbose)
			printf("\nCalls transformed: %d\n", num);
		return true;
	} else {
		int start = hunk->FindSymbol("_CallTrans")->value;
		int size = hunk->FindSymbol("_CallTransSize")->value;
		memset(hunk->GetPtr()+start, 0x90, codeSize);
		if (verbose)
			Log::Warning("", "No calls - call transformation not applied");
		// Do not run call trans next time
		Disable();
		return false;
	}
}