#include "HeuristicHunkSorter.h"
#include "HunkList.h"
#include "Hunk.h"
#include "Symbol.h"
#include <vector>
#include <algorithm>

using namespace std;

bool hunkRelation(Hunk* h1, Hunk* h2) {
	//initialized data < uninitialized data
	if((h1->getRawSize() != 0) != (h2->getRawSize() != 0))
		return h1->getRawSize() > h2->getRawSize();

	//code < non-code
	if((h1->getFlags() & HUNK_IS_CODE) != (h2->getFlags() & HUNK_IS_CODE))
		return (h1->getFlags() & HUNK_IS_CODE);

	if(strcmp(h1->getName(), "ImportListHunk")==0)
		return true;
	if(strcmp(h2->getName(), "ImportListHunk")==0)
		return false;

	if(h1->getRawSize() == 0)
		return h1->getVirtualSize() < h2->getVirtualSize();

	if (h1->getFlags() & HUNK_IS_TRAILING)
		return false;
	if (h2->getFlags() & HUNK_IS_TRAILING)
		return true;

	if(h1->getAlignmentBits() == h2->getAlignmentBits()) {
		if(h1->getRawSize() != h2->getRawSize())
			return h1->getRawSize() < h2->getRawSize();
		else {
			{
				//compare data
				return memcmp(h1->getPtr(), h2->getPtr(), h1->getRawSize()) > 0;
			}
		}				
	}
	return h1->getAlignmentBits() < h2->getAlignmentBits();
}

void HeuristicHunkSorter::sortHunkList(HunkList* hunklist) {
	vector<Hunk*> hunks;

	Hunk *import_hunk = hunklist->findSymbol("_Import")->hunk;
	Hunk *entry_hunk = import_hunk->getContinuation()->hunk;
	Hunk *initializer_hunk = NULL;
	if (entry_hunk->getContinuation() != NULL) {
		initializer_hunk = entry_hunk;
		entry_hunk = initializer_hunk->getContinuation()->hunk;
	}

	hunklist->removeHunk(import_hunk);
	if (initializer_hunk) hunklist->removeHunk(initializer_hunk);
	hunklist->removeHunk(entry_hunk);

	//move hunks to vector
	for(int i = 0; i < hunklist->getNumHunks(); i++) {
		Hunk* h = (*hunklist)[i];
		hunks.push_back(h);
	}
	hunklist->clear();

	//sort hunks
	sort(hunks.begin(), hunks.end(), hunkRelation);

	hunklist->addHunkBack(import_hunk);
	if (initializer_hunk) hunklist->addHunkBack(initializer_hunk);
	hunklist->addHunkBack(entry_hunk);

	//copy back hunks to hunklist
	for(vector<Hunk*>::const_iterator it = hunks.begin(); it != hunks.end(); it++)
		hunklist->addHunkBack(*it);	
}