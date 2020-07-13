#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"
#include "Symbol.h"
#include "Crinkler.h"

bool Transform::linkAndTransform(HunkList* hunklist, Symbol *entry_label, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose)
{
	Hunk* detrans = nullptr;
	if (m_enabled)
	{
		detrans = getDetransformer();
		if(detrans)
		{
			detrans->setVirtualSize(detrans->getRawSize());
		}
	}

	if(!detrans)
	{
		detrans = new Hunk("Stub", NULL, HUNK_IS_CODE, 0, 0, 0);
	}
	hunklist->addHunkFront(detrans);
	detrans->setContinuation(entry_label);

	int sp;
	transformedHunk = hunklist->toHunk("linked", baseAddress, &sp);
	transformedHunk->relocate(baseAddress);

	if (splittingPoint)
	{
		*splittingPoint = sp;
	}

	if (untransformedHunk)
	{
		*untransformedHunk = new Hunk(*transformedHunk);
	}

	hunklist->removeHunk(detrans);
	delete detrans;

	return m_enabled ? transform(transformedHunk, sp, verbose) : false;
}