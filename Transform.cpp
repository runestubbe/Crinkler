#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"
#include "Symbol.h"
#include "Crinkler.h"

bool Transform::linkAndTransform(HunkList* hunklist, Symbol *entry_label, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose) {
	Hunk* detrans = getDetransformer();

	if(!detrans) {
		detrans = new Hunk("Stub", NULL, HUNK_IS_CODE, 0, 0, 0);
	}
	detrans->setVirtualSize(detrans->getRawSize());
	detrans->addSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, detrans));
	hunklist->addHunkFront(detrans);
	detrans->setContinuation(entry_label);
	int sp;
	transformedHunk = hunklist->toHunk("linked", &sp);
	transformedHunk->relocate(baseAddress);
	if(untransformedHunk)
		*untransformedHunk = new Hunk(*transformedHunk);
	
	hunklist->removeHunk(detrans);
	delete detrans;
	if(splittingPoint)
		*splittingPoint = sp;

	return transform(transformedHunk, sp, verbose);
}