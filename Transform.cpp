#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"
#include "Symbol.h"
#include "Crinkler.h"

bool Transform::linkAndTransform(HunkList* hunklist, Symbol *entry_label, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose) {
	Hunk* detrans;
	if (m_enabled) {
		detrans = getDetransformer();

		if(!detrans) {
			detrans = new Hunk("Stub", NULL, HUNK_IS_CODE, 0, 0, 0);
		}
		detrans->setVirtualSize(detrans->getRawSize());
		hunklist->addHunkFront(detrans);
		detrans->setContinuation(entry_label);
	}
	int sp;
	transformedHunk = hunklist->toHunk("linked", baseAddress, &sp);
	transformedHunk->relocate(baseAddress);
	if(untransformedHunk)
		*untransformedHunk = new Hunk(*transformedHunk);

	if(splittingPoint)
		*splittingPoint = sp;

	if (m_enabled) {
		hunklist->removeHunk(detrans);
		delete detrans;

		return transform(transformedHunk, sp, verbose);
	} else {
		return false;
	}
}