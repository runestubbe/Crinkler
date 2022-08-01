#include "HeuristicHunkSorter.h"
#include "PartList.h"
#include "Hunk.h"
#include "Symbol.h"
#include <vector>
#include <algorithm>

using namespace std;

static bool HunkRelation(Hunk* h1, Hunk* h2) {
	// Initialized data < uninitialized data
	if((h1->GetRawSize() != 0) != (h2->GetRawSize() != 0))
		return h1->GetRawSize() > h2->GetRawSize();

	// Code < non-code
	if((h1->GetFlags() & HUNK_IS_CODE) != (h2->GetFlags() & HUNK_IS_CODE))
		return (h1->GetFlags() & HUNK_IS_CODE);

	if(strcmp(h1->GetName(), "ImportListHunk")==0)
		return true;
	if(strcmp(h2->GetName(), "ImportListHunk")==0)
		return false;

	if(h1->GetRawSize() == 0)
		return h1->GetVirtualSize() < h2->GetVirtualSize();

	if(h1->GetFlags() & HUNK_IS_LEADING)
		return true;
	if(h2->GetFlags() & HUNK_IS_LEADING)
		return false;


	if (h1->GetFlags() & HUNK_IS_TRAILING)
		return false;
	if (h2->GetFlags() & HUNK_IS_TRAILING)
		return true;

	if(h1->GetAlignmentBits() == h2->GetAlignmentBits()) {
		if(h1->GetRawSize() != h2->GetRawSize())
			return h1->GetRawSize() < h2->GetRawSize();
		else {
			{
				// Compare data
				return memcmp(h1->GetPtr(), h2->GetPtr(), h1->GetRawSize()) > 0;
			}
		}				
	}
	return h1->GetAlignmentBits() < h2->GetAlignmentBits();
}

void HeuristicHunkSorter::SortHunkList(PartList& parts) {
	
	Hunk *import_hunk = parts.FindSymbol("_Import")->hunk;
	Hunk *entry_hunk = import_hunk->GetContinuation()->hunk;
	Hunk *initializer_hunk = NULL;
	
	if (entry_hunk->GetContinuation() != NULL) {
		initializer_hunk = entry_hunk;
		entry_hunk = initializer_hunk->GetContinuation()->hunk;
	}

	Part& codePart = parts.GetCodePart();
	codePart.RemoveHunk(import_hunk);
	if (initializer_hunk) codePart.RemoveHunk(initializer_hunk);
	codePart.RemoveHunk(entry_hunk);

	// Sort hunks
	parts.ForEachPart([](Part& part, int index)
		{
			sort(part.m_hunks.begin(), part.m_hunks.end(), HunkRelation);
		});

	codePart.AddHunkFront(entry_hunk);
	if (initializer_hunk) codePart.AddHunkFront(initializer_hunk);
	codePart.AddHunkFront(import_hunk);
	
	
}
