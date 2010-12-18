#include "EmpiricalHunkSorter.h"
#include <ctime>
#include <cmath>
#include "HunkList.h"
#include "Hunk.h"
#include "Compressor/CompressionStream.h"
#include "Compressor/ProgressBar.h"
#include "Crinkler.h"

using namespace std;

EmpiricalHunkSorter::EmpiricalHunkSorter() {
}

EmpiricalHunkSorter::~EmpiricalHunkSorter() {
}

int EmpiricalHunkSorter::tryHunkCombination(HunkList* hunklist, Transform& transform, ModelList& codeModels, ModelList& dataModels, int baseprob) {
	int splittingPoint;

	Hunk* phase1;
	Symbol* import = hunklist->findSymbol("_Import");
	transform.linkAndTransform(hunklist, import, CRINKLER_CODEBASE, phase1, NULL, &splittingPoint, false);
	
	char contexts[2][8];
	memset(contexts[0], 0, 8);
	assert(splittingPoint >= 8);
	memcpy(contexts[1], phase1->getPtr()+splittingPoint-8, 8);

	CompressionStream cs(NULL, NULL, 0);
	int sizes[16];
	#pragma omp parallel for
	for(int i = 0; i < 16; i++) {
		if(i < 8)
			sizes[i] = cs.EvaluateSizeQuick((unsigned char*)phase1->getPtr(), splittingPoint, codeModels, baseprob, contexts[0], i);
		else
			sizes[i] = cs.EvaluateSizeQuick((unsigned char*)phase1->getPtr()+splittingPoint, phase1->getRawSize()-splittingPoint, dataModels, baseprob, contexts[1], i - 8);
	}
	delete phase1;

	int size = 0;
	for(int i = 0; i < 16; i++)
		size += sizes[i];

	return size;
}

void permuteHunklist(HunkList* hunklist, int strength) {
	int n_permutes = (rand() % strength) + 1;
	for (int p = 0 ; p < n_permutes ; p++)
	{
		int fixedHunks;
		int h1i, h2i;
		int sections[3];
		int nHunks = hunklist->getNumHunks();
		fixedHunks = 0;
		int codeHunks = 0;
		int dataHunks = 0;
		int uninitHunks = 0;

		{
			//count different types of hunks
			while(fixedHunks < nHunks && (*hunklist)[fixedHunks]->getFlags() & HUNK_IS_FIXED)
				fixedHunks++;
			codeHunks = fixedHunks;
			while(codeHunks < nHunks && (*hunklist)[codeHunks]->getFlags() & HUNK_IS_CODE)
				codeHunks++;
			dataHunks = codeHunks;
			while(dataHunks < nHunks && (*hunklist)[dataHunks]->getRawSize() > 0)
				dataHunks++;
			uninitHunks = nHunks - dataHunks;
			dataHunks -= codeHunks;
			codeHunks -= fixedHunks;
			if (codeHunks < 2 && dataHunks < 2 && uninitHunks < 2) return;
			sections[0] = codeHunks;
			sections[1] = dataHunks;
			sections[2] = uninitHunks;
			nHunks -= fixedHunks;
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
		int base = fixedHunks + (s > 0 ? sections[0] : 0) + (s > 1 ? sections[1] : 0);

		if (h2i < h1i)
		{
			// Insert before
			for (int i = 0 ; i < n ; i++)
			{
				hunklist->insertHunk(base+h2i+i, hunklist->removeHunk((*hunklist)[base+h1i+i]));
			}
		} else {
			// Insert after
			for (int i = 0 ; i < n ; i++)
			{
				hunklist->insertHunk(base+h2i+n-1, hunklist->removeHunk((*hunklist)[base+h1i]));
			}
		}
	}
}

void randomPermute(HunkList* hunklist) {
	bool done = false;
	int h1i, h2i;
	
	int sections[3];
	int nHunks = hunklist->getNumHunks();
	int fixedHunks = 0;
	int codeHunks = 0;
	int dataHunks = 0;
	int uninitHunks = 0;

	//count different types of hunks
	{
		while(fixedHunks < nHunks && (*hunklist)[fixedHunks]->getFlags() & HUNK_IS_FIXED)
			fixedHunks++;
		codeHunks = fixedHunks;
		while(codeHunks < nHunks && (*hunklist)[codeHunks]->getFlags() & HUNK_IS_CODE)
			codeHunks++;
		dataHunks = codeHunks;
		while(dataHunks < nHunks && (*hunklist)[dataHunks]->getRawSize() > 0)
			dataHunks++;
		uninitHunks = nHunks - dataHunks;
		dataHunks -= codeHunks;
		codeHunks -= fixedHunks;
		sections[0] = codeHunks;
		sections[1] = dataHunks;
		sections[2] = uninitHunks;
		nHunks -= fixedHunks;
	}

	int idx = fixedHunks;
	for(int j = 0; j < 3; j++) {
		for(int i = 0; i < sections[j]; i++) {
			int swapidx = rand() % (sections[j]-i);
			swap((*hunklist)[idx+i], (*hunklist)[idx+swapidx]);
		}
		idx += sections[j];
	}
}

void EmpiricalHunkSorter::sortHunkList(HunkList* hunklist, Transform& transform, ModelList& codeModels, ModelList& dataModels, int baseprob, int numIterations, ProgressBar* progress) {
	int sections[3];
	int fixedHunks = 0;
	int nHunks = hunklist->getNumHunks();

	printf("\n\nReordering sections...\n");
	fflush(stdout);

	while(fixedHunks < nHunks && (*hunklist)[fixedHunks]->getFlags() & HUNK_IS_FIXED)
		fixedHunks++;
	nHunks -= fixedHunks;

	int bestsize = tryHunkCombination(hunklist, transform, codeModels, dataModels, baseprob);
	
	if(progress)
		progress->beginTask("Reordering sections");

	Hunk** backup = new Hunk*[nHunks];
	int fails = 0;
	int stime = clock();
	for(int i = 0; i < numIterations; i++) {
		for(int j = 0; j < nHunks; j++)
			backup[j] = (*hunklist)[j+fixedHunks];

		permuteHunklist(hunklist, 2/*(int)sqrt((double)fails)/10+1*/);
		//randomPermute(hunklist);

		int size = tryHunkCombination(hunklist, transform, codeModels, dataModels, baseprob);
		//printf("size: %5.2f\n", size / (BITPREC * 8.0f));
		if(size < bestsize) {
			printf("  Iteration: %5d  Size: %5.2f\n", i, size / (BITPREC * 8.0f));
			fflush(stdout);
			bestsize = size;
			fails = 0;
		} else {
			fails++;
			//restore from backup
			for(int j = 0; j < nHunks; j++)
				(*hunklist)[j+fixedHunks] = backup[j];
		}
		if(progress)
			progress->update(i+1, numIterations);
	}
	if(progress)
		progress->endTask();

	delete[] backup;
	int timespent = (clock() - stime)/CLOCKS_PER_SEC;
	printf("Time spent: %dm%02ds\n", timespent/60, timespent%60);
}