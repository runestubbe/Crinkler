#include "Transform.h"
#include "Hunk.h"
#include "PartList.h"
#include "Log.h"
#include "Symbol.h"
#include "Crinkler.h"

bool Transform::LinkAndTransform(PartList& parts, Symbol *entryLabel, int baseAddress, Hunk** transformedHunk, Hunk** untransformedHunk, bool verbose)
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
	
	Part& codePart = parts.GetCodePart();
	codePart.AddHunkFront(detrans);
	detrans->SetContinuation(entryLabel);

	*transformedHunk = parts.Link("linked", baseAddress);
	(*transformedHunk)->Relocate(baseAddress);

	if (untransformedHunk)
	{
		*untransformedHunk = new Hunk(**transformedHunk);
	}

	codePart.RemoveHunk(detrans);
	delete detrans;

	return m_enabled ? DoTransform(*transformedHunk, codePart.GetLinkedSize(), verbose) : false;
}