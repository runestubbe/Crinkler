#include "EmpiricalHunkSorter.h"
#include <ctime>
#include <cmath>
#include <ppl.h>
#include "PartList.h"
#include "Hunk.h"
#include "../Compressor/CompressionStream.h"
#include "ProgressBar.h"
#include "Crinkler.h"

using namespace std;

static void PermuteHunklist(PartList& parts, int strength) {

	const int numParts = parts.GetNumParts();
	int numTotalHunks = 0;
	for (int partIndex = 0; partIndex < numParts; partIndex++) {
		const int numHunks = parts[partIndex].GetNumHunks();
		numTotalHunks += numHunks;
	}
	
	int numPermutes = (rand() % strength) + 1;	//TODO
	for (int p = 0 ; p < numPermutes; p++)
	{
		int t = rand() % numTotalHunks;
		int partIndex = 0;
		while (t >= parts[partIndex].GetNumHunks()) {
			t -= parts[partIndex].GetNumHunks();
			partIndex++;
		}

		Part& part = parts[partIndex];
		if (part.GetNumHunks() < 2)
			continue;

		int maxN = part.GetNumHunks() / 2;
		if (maxN > strength) maxN = strength;
		int n = (rand() % maxN) + 1;
		int h1i = rand() % (part.GetNumHunks() - n + 1);
		int h2i;
		do {
			h2i = rand() % (part.GetNumHunks() - n + 1);
		} while (h2i == h1i);

		if (h2i < h1i) {
			// Insert before
			for (int i = 0 ; i < n ; i++) {
				Hunk* hunk = part[h1i + i];
				part.RemoveHunk(hunk);
				part.InsertHunk(h2i+i, hunk);
			}
		} else {
			// Insert after
			for (int i = 0 ; i < n ; i++) {
				Hunk* hunk = part[h1i];
				part.RemoveHunk(hunk);
				part.InsertHunk(h2i+n-1, hunk);
			}
		}
		// RUNE_TODO: handle trailing hunks
	}
}

int EmpiricalHunkSorter::TryHunkCombination(PartList& parts, Transform& transform, bool saturate, bool use1KMode)
{
	Hunk* phase1;
	Symbol* import = parts.FindSymbol("_Import");
	transform.LinkAndTransform(parts, import, CRINKLER_CODEBASE, phase1, NULL, false);

	int totalsize = 0;
	if (use1KMode)
	{
		assert(parts.GetNumInitializedParts() == 1);

		int max_size = phase1->GetRawSize() * 2 + 1000;
		unsigned char* compressed_data_ptr = new unsigned char[max_size];
		Compress1k((unsigned char*)phase1->GetPtr(), phase1->GetRawSize(), compressed_data_ptr, max_size, parts[0].m_model1k, nullptr, &totalsize);	//TODO: Estimate instead of compress
		delete[] compressed_data_ptr;
	}
	else
	{
		const int numInitializedParts = parts.GetNumInitializedParts();
		
		std::vector<ModelList4k*> modelLists(numInitializedParts);
		std::vector<int> partSizes(numInitializedParts);
		std::vector<int> compressedSizes(numInitializedParts);
		
		for (int i = 0; i < numInitializedParts; i++) {
			Part& part = parts[i];
			modelLists[i] = &part.m_model4k;
			partSizes[i] = part.GetLinkedSize();
		}

		totalsize = EvaluateSize4k((unsigned char*)phase1->GetPtr(), numInitializedParts, partSizes.data(), compressedSizes.data(), modelLists.data(), CRINKLER_BASEPROB, saturate);

		for (int i = 0; i < numInitializedParts; i++) {
			parts[i].m_compressedSize = compressedSizes[i];
		}
		
		delete phase1;
	}

	return totalsize;
}

static void PrintIterationStats(PartList& parts, int iteration, int totalSize, bool use1KMode)
{
	if (use1KMode)
	{
		printf("  Iteration: %5d  Size: %5.2f\n", iteration, totalSize / (BIT_PRECISION * 8.0f));
	}
	else
	{
		printf("  Iteration: %5d", iteration);
	
		parts.ForEachPart([](Part& part, int index) {
			if (part.IsInitialized())
			{
				printf("  %s: %.2f", part.GetName(), part.m_compressedSize / (BIT_PRECISION * 8.0f));
			}
			});

		printf("  Size: %5.2f\n", totalSize / (BIT_PRECISION * 8.0f));
	}
}

int EmpiricalHunkSorter::SortHunkList(PartList& parts, Transform& transform, bool saturate, int numIterations, ProgressBar* progress, bool use1KMode)
{
	srand(1);
	
	printf("\n\nReordering sections...\n");
	fflush(stdout);
	
	int bestTotalSize = TryHunkCombination(parts, transform, saturate, use1KMode);

	const int stime = clock();

	PrintIterationStats(parts, 0, bestTotalSize, use1KMode);
	
	if(progress)
		progress->BeginTask("Reordering sections");

	int numTotalHunks = 0;
	parts.ForEachPart([&numTotalHunks](Part& part, int index) {
		numTotalHunks += part.GetNumHunks();
		});

	Hunk** backup = new Hunk*[numTotalHunks];
	for(int i = 1; i < numIterations; i++) {
		
		{
			//TODO: this is ugly
			int offset = 0;
			parts.ForEachPart([&](Part& part, int index) {
				const int numHunks = part.GetNumHunks();
				for (int i = 0; i < numHunks; i++) {
					backup[offset + i] = part[i];
				}
				offset += numHunks;
				});
		}

		PermuteHunklist(parts, 2);
		
		int totalSize = TryHunkCombination(parts, transform, saturate, use1KMode);
		if(totalSize < bestTotalSize) {
			bestTotalSize = totalSize;
			PrintIterationStats(parts, i, bestTotalSize, use1KMode);
			
			fflush(stdout);
		}
		else
		{
			{
				//TODO: this is ugly
				int offset = 0;
				parts.ForEachPart([&](Part& part, int index) {
					const int numHunks = part.GetNumHunks();
					for (int i = 0; i < numHunks; i++) {
						part[i] = backup[offset + i];
					}
					offset += numHunks;
					});
			}
		}
		if(progress)
			progress->Update(i+1, numIterations);
	}
	delete[] backup;
	if(progress)
		progress->EndTask();

	int timespent = (clock() - stime)/CLOCKS_PER_SEC;
	printf("Time spent: %dm%02ds\n", timespent/60, timespent%60);
	return bestTotalSize;
}