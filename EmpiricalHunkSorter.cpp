#include "EmpiricalHunkSorter.h"
#include <cstdio>
#include <ctime>
#include "HunkList.h"
#include "Hunk.h"
#include "Compressor/CompressionStream.h"
#include "Compressor/ProgressBar.h"

using namespace std;

EmpiricalHunkSorter::EmpiricalHunkSorter() {
}

EmpiricalHunkSorter::~EmpiricalHunkSorter() {
}

int EmpiricalHunkSorter::tryHunkCombination(HunkList* hunklist, ModelList& codeModels, ModelList& dataModels, int baseprobs[8]) {
	int splittingPoint;
	int imageBase = 0x400000;
	int sectionSize = 0x10000;

	Hunk* phase1 = hunklist->toHunk("linkedHunk", &splittingPoint);
	phase1->relocate(imageBase+sectionSize*2);
	
	char contexts[2][8];
	memset(contexts[0], 0, 8);
	assert(splittingPoint >= 8);
	memcpy(contexts[1], phase1->getPtr()+splittingPoint-8, 8);

	CompressionStream cs(NULL, NULL, 0);
	int sizes[16];
	#pragma omp parallel for
	for(int i = 0; i < 16; i++) {
		if(i < 8)
			sizes[i] = cs.EvaluateSizeQuick((unsigned char*)phase1->getPtr(), splittingPoint, codeModels, baseprobs, contexts[0], i);
		else
			sizes[i] = cs.EvaluateSizeQuick((unsigned char*)phase1->getPtr()+splittingPoint, phase1->getRawSize()-splittingPoint, dataModels, baseprobs, contexts[1], i - 8);
	}
	delete phase1;

	int size = 0;
	for(int i = 0; i < 16; i++)
		size += sizes[i];

	return size;
}

void permuteHunklist(HunkList* hunklist) {
	bool done = false;
	int fixedHunks;
	int h1i, h2i;
	do {
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
			sections[0] = codeHunks;
			sections[1] = dataHunks;
			sections[2] = uninitHunks;
			nHunks -= fixedHunks;
		}

		h1i = rand() % nHunks;
		int s = 0;
		int accum = 0;
		//find section
		while(h1i >= accum + sections[s]) {
			accum += sections[s];
			s++;
		}
		assert(s < 3);

		h2i = rand() % sections[s] + accum;
		if(h1i != h2i)
			done = true;
	} while(!done);	

	hunklist->insertHunk(h2i+fixedHunks, hunklist->removeHunk((*hunklist)[h1i+fixedHunks]));
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



void EmpiricalHunkSorter::sortHunkList(HunkList* hunklist, ModelList& codeModels, ModelList& dataModels, int baseprobs[8], int numIterations, ProgressBar* progress) {
	int sections[3];
	int fixedHunks = 0;
	int nHunks = hunklist->getNumHunks();

	printf("\n\nReordering sections...\n");
	fflush(stdout);

	while(fixedHunks < nHunks && (*hunklist)[fixedHunks]->getFlags() & HUNK_IS_FIXED)
		fixedHunks++;
	nHunks -= fixedHunks;

	int bestsize = tryHunkCombination(hunklist, codeModels, dataModels, baseprobs);
	
	if(progress)
		progress->beginTask("Reordering sections");

	Hunk** backup = new Hunk*[nHunks];
	int fails = 0;
	int stime = clock();
	for(int i = 0; i < numIterations; i++) {
		for(int j = 0; j < nHunks; j++)
			backup[j] = (*hunklist)[j+fixedHunks];

		permuteHunklist(hunklist);

		int size = tryHunkCombination(hunklist, codeModels, dataModels, baseprobs);
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