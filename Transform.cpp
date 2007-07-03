#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"

Hunk* Transform::linkAndTransform(HunkList* hunklist, int baseAddress, int* splittingPoint) {
	Hunk* detrans = getDetransformer();

	if(detrans)
		hunklist->addHunkFront(detrans);
	int sp;
	//
	Hunk* h = hunklist->toHunk("linked", &sp);
	h->relocate(baseAddress);
	
	delete detrans;
	hunklist->removeHunk(detrans);

	if(!transform(h, sp)) {	//transform failed, link without transform
		delete h;
		Hunk* h = hunklist->toHunk("linked", &sp);
		h->relocate(baseAddress);
	}

	if(splittingPoint)
		*splittingPoint = sp;

	return h;
}