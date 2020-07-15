#include "HeuristicHunkSorter.h"
#include "HunkList.h"
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

void HeuristicHunkSorter::SortHunkList(HunkList* hunklist) {
	vector<Hunk*> hunks;

	Hunk *import_hunk = hunklist->FindSymbol("_Import")->hunk;
	Hunk *entry_hunk = import_hunk->GetContinuation()->hunk;
	Hunk *initializer_hunk = NULL;
	if (entry_hunk->GetContinuation() != NULL) {
		initializer_hunk = entry_hunk;
		entry_hunk = initializer_hunk->GetContinuation()->hunk;
	}

	hunklist->RemoveHunk(import_hunk);
	if (initializer_hunk) hunklist->RemoveHunk(initializer_hunk);
	hunklist->RemoveHunk(entry_hunk);

	// Move hunks to vector
	for(int i = 0; i < hunklist->GetNumHunks(); i++) {
		Hunk* h = (*hunklist)[i];
		hunks.push_back(h);
	}
	hunklist->Clear();

	// Sort hunks
	sort(hunks.begin(), hunks.end(), HunkRelation);

	hunklist->AddHunkBack(import_hunk);
	if (initializer_hunk) hunklist->AddHunkBack(initializer_hunk);
	hunklist->AddHunkBack(entry_hunk);

	// Copy hunks back to hunklist
	for(Hunk *hunk : hunks) hunklist->AddHunkBack(hunk);
}
