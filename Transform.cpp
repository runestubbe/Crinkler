#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"
#include "Symbol.h"
#include "Crinkler.h"

bool Transform::linkAndTransform(HunkList* hunklist, Symbol *entry_label, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose, bool b1kMode)
{
	Hunk* detrans;
	if (m_enabled)
	{
		detrans = getDetransformer();

		if (!detrans)
		{
			detrans = new Hunk("Stub", NULL, HUNK_IS_CODE, 0, 0, 0);
		}
		detrans->setVirtualSize(detrans->getRawSize());
		hunklist->addHunkFront(detrans);
		detrans->setContinuation(entry_label);
	}

	Hunk* initialZeroByteHunk = nullptr;
	if (b1kMode)
	{
		initialZeroByteHunk = new Hunk("1KZero", NULL, HUNK_IS_CODE, 0, 2, 2);
		
		initialZeroByteHunk->addSymbol(new Symbol(".text", 0, SYMBOL_IS_RELOCATEABLE | SYMBOL_IS_SECTION, initialZeroByteHunk, "crinkler"));
		
		char* ptr = initialZeroByteHunk->getPtr();
		ptr[0] = 0x00;
		ptr[1] = 0xC1;

		hunklist->addHunkFront(initialZeroByteHunk);
		if (!detrans)
		{
			initialZeroByteHunk->setContinuation(entry_label);
		}
	}

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

	if (initialZeroByteHunk)
	{
		hunklist->removeHunk(initialZeroByteHunk);
		delete initialZeroByteHunk;
	}

	if (m_enabled)
	{
		hunklist->removeHunk(detrans);
		delete detrans;

		return transform(transformedHunk, sp, verbose);
	}
	else
	{
		return false;
	}
}