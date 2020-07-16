#include "EmpiricalHunkSorter.h"
#include <ctime>
#include <cmath>
#include <ppl.h>
#include "HunkList.h"
#include "Hunk.h"
#include "../Compressor/CompressionStream.h"
#include "ProgressBar.h"
#include "Crinkler.h"

using namespace std;

static void PermuteHunklist(HunkList* hunklist, int strength) {
	int n_permutes = (rand() % strength) + 1;
	for (int p = 0 ; p < n_permutes ; p++)
	{
		int h1i, h2i;
		int sections[3];
		int nHunks = hunklist->GetNumHunks();
		int codeHunks = 0;
		int dataHunks = 0;
		int uninitHunks = 0;

		{
			// Count different types of hunks
			codeHunks = 0;
			while(codeHunks < nHunks && (*hunklist)[codeHunks]->GetFlags() & HUNK_IS_CODE)
				codeHunks++;
			dataHunks = codeHunks;
			while(dataHunks < nHunks && (*hunklist)[dataHunks]->GetRawSize() > 0)
				dataHunks++;
			uninitHunks = nHunks - dataHunks;
			dataHunks -= codeHunks;
			if (codeHunks < 2 && dataHunks < 2 && uninitHunks < 2) return;
			sections[0] = codeHunks;
			sections[1] = dataHunks;
			sections[2] = uninitHunks;
		}

		int s;
		do {
			s = rand() % 3;
		} while (sections[s] < 2);
		int max_n = sections[s]/2;
		if (max_n > strength) max_n = strength;
		int n = (rand() % max_n) + 1;
		h1i = rand() % (sections[s] - n + 1);
		do {
			h2i = rand() % (sections[s] - n + 1);
		} while (h2i == h1i);
		int base = (s > 0 ? sections[0] : 0) + (s > 1 ? sections[1] : 0);

		if (h2i < h1i)
		{
			// Insert before
			for (int i = 0 ; i < n ; i++)
			{
				hunklist->InsertHunk(base+h2i+i, hunklist->RemoveHunk((*hunklist)[base+h1i+i]));
			}
		} else {
			// Insert after
			for (int i = 0 ; i < n ; i++)
			{
				hunklist->InsertHunk(base+h2i+n-1, hunklist->RemoveHunk((*hunklist)[base+h1i]));
			}
		}
	}
}

EmpiricalHunkSorter::EmpiricalHunkSorter() {
}

EmpiricalHunkSorter::~EmpiricalHunkSorter() {
}

int EmpiricalHunkSorter::TryHunkCombination(HunkList* hunklist, Transform& transform, ModelList4k& codeModels, ModelList4k& dataModels, ModelList1k& models1k, int baseprob, bool saturate, bool use1KMode, int* out_size1, int* out_size2)
{
	int splittingPoint;

	Hunk* phase1;
	Symbol* import = hunklist->FindSymbol("_Import");
	transform.LinkAndTransform(hunklist, import, CRINKLER_CODEBASE, phase1, NULL, &splittingPoint, false);

	int totalsize = 0;
	if (use1KMode)
	{
		int max_size = phase1->GetRawSize() * 2 + 1000;
		unsigned char* compressed_data_ptr = new unsigned char[max_size];
		Compress1k((unsigned char*)phase1->GetPtr(), phase1->GetRawSize(), compressed_data_ptr, max_size, models1k, nullptr, &totalsize);	//TODO: Estimate instead of compress
		delete[] compressed_data_ptr;

		if(out_size1) *out_size1 = totalsize;
		if(out_size2) *out_size2 = 0;
	}
	else
	{
		ModelList4k* ModelLists[] = { &codeModels, &dataModels };
		int sectionSizes[] = {splittingPoint, phase1->GetRawSize() - splittingPoint};
		int compressedSizes[2] = {};
		totalsize = EvaluateSize4k((unsigned char*)phase1->GetPtr(), 2, sectionSizes, compressedSizes, ModelLists, baseprob, saturate);
		
		if (out_size1) *out_size1 = compressedSizes[0];
		if (out_size2) *out_size2 = compressedSizes[1];

		delete phase1;
	}

	return totalsize;
}

int EmpiricalHunkSorter::SortHunkList(HunkList* hunklist, Transform& transform, ModelList4k& codeModels, ModelList4k& dataModels, ModelList1k& models1k, int baseprob, bool saturate, int numIterations, ProgressBar* progress, bool use1KMode, int* out_size1, int* out_size2)
{
	srand(1);

	int nHunks = hunklist->GetNumHunks();
	
	printf("\n\nReordering sections...\n");
	fflush(stdout);
	
	int best_size1;
	int best_size2;
	int best_total_size = TryHunkCombination(hunklist, transform, codeModels, dataModels, models1k, baseprob, saturate, use1KMode, &best_size1, &best_size2);
	if(use1KMode)
	{
		printf("  Iteration: %5d  Size: %5.2f\n", 0, best_total_size / (BIT_PRECISION * 8.0f));
	}
	else
	{
		printf("  Iteration: %5d  Code: %.2f  Data: %.2f  Size: %.2f\n", 0, best_size1 / (BIT_PRECISION * 8.0f), best_size2 / (BIT_PRECISION * 8.0f), best_total_size / (BIT_PRECISION * 8.0f));
	}
	
	if(progress)
		progress->BeginTask("Reordering sections");

	Hunk** backup = new Hunk*[nHunks];
	int fails = 0;
	int stime = clock();
	for(int i = 1; i < numIterations; i++) {
		for(int j = 0; j < nHunks; j++)
			backup[j] = (*hunklist)[j];
		
		// Save DLL hunk
		Hunk* dllhunk = nullptr;
		int dlli;
		for(dlli = 0; dlli < hunklist->GetNumHunks(); dlli++) {
			if((*hunklist)[dlli]->GetFlags() & HUNK_IS_LEADING) {
				dllhunk = (*hunklist)[dlli];
				hunklist->RemoveHunk(dllhunk);
				break;
			}
		}
		

		Hunk* eh = nullptr;
		int ehi;
		for (ehi = 0; ehi < hunklist->GetNumHunks(); ehi++) {
			if ((*hunklist)[ehi]->GetFlags() & HUNK_IS_TRAILING) {
				eh = (*hunklist)[ehi];
				hunklist->RemoveHunk(eh);
				break;
			}
		}

		PermuteHunklist(hunklist, 2);
		
		// Restore export hunk, if present
		if (eh) {
			hunklist->InsertHunk(ehi, eh);
		}
		
		if(dllhunk)
		{
			hunklist->InsertHunk(dlli, dllhunk);
		}


		int size1, size2;
		int total_size = TryHunkCombination(hunklist, transform, codeModels, dataModels, models1k, baseprob, saturate, use1KMode, &size1, &size2);
		if(total_size < best_total_size) {
			if(use1KMode)
			{
				printf("  Iteration: %5d  Size: %5.2f\n", i, total_size / (BIT_PRECISION * 8.0f));
			}
			else
			{
				printf("  Iteration: %5d  Code: %.2f  Data: %.2f  Size: %.2f\n", i, size1 / (BIT_PRECISION * 8.0f), size2 / (BIT_PRECISION * 8.0f), total_size / (BIT_PRECISION * 8.0f));
			}
			
			fflush(stdout);
			best_total_size = total_size;
			best_size1 = size1;
			best_size2 = size2;
			fails = 0;
		} else {
			fails++;
			// Restore from backup
			for(int j = 0; j < nHunks; j++)
				(*hunklist)[j] = backup[j];
		}
		if(progress)
			progress->Update(i+1, numIterations);
	}
	if(progress)
		progress->EndTask();

	if(out_size1) *out_size1 = best_size1;
	if(out_size2) *out_size2 = best_size2;

	delete[] backup;
	int timespent = (clock() - stime)/CLOCKS_PER_SEC;
	printf("Time spent: %dm%02ds\n", timespent/60, timespent%60);
	return best_total_size;
}