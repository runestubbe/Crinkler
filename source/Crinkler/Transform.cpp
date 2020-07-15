#include "Transform.h"
#include "Hunk.h"
#include "HunkList.h"
#include "Log.h"
#include "Symbol.h"
#include "Crinkler.h"

bool Transform::LinkAndTransform(HunkList* hunklist, Symbol *entry_label, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose)
{
	Hunk* detrans = nullptr;
	if (m_enabled)
	{
		detrans = GetDetransformer();
		if(detrans)
		{
			detrans->SetVirtualSize(detrans->GetRawSize());
		}
	}

	if(!detrans)
	{
		detrans = new Hunk("Stub", NULL, HUNK_IS_CODE, 0, 0, 0);
	}
	hunklist->AddHunkFront(detrans);
	detrans->SetContinuation(entry_label);

	int sp;
	transformedHunk = hunklist->ToHunk("linked", baseAddress, &sp);
	transformedHunk->Relocate(baseAddress);

	if (splittingPoint)
	{
		*splittingPoint = sp;
	}

	if (untransformedHunk)
	{
		*untransformedHunk = new Hunk(*transformedHunk);
	}

	hunklist->RemoveHunk(detrans);
	delete detrans;

	return m_enabled ? DoTransform(transformedHunk, sp, verbose) : false;
}