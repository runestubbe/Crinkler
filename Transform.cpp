#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"

bool Transform::linkAndTransform(HunkList* hunklist, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose) {
	Hunk* detrans = getDetransformer();

	if(detrans)
		hunklist->addHunkFront(detrans);
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