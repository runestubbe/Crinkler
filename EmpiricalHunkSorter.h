#pragma once
#ifndef _EMPIRICAL_HUNK_SORTER_H_
#define _EMPIRICAL_HUNK_SORTER_H_

class HunkList;
class ModelList;
class ProgressBar;
class Transform;
class EmpiricalHunkSorter {
	static int tryHunkCombination(HunkList* hunklist, Transform& transform, ModelList& codeModels, ModelList& dataModels, int baseprobs[8]);
public:
	EmpiricalHunkSorter();
	~EmpiricalHunkSorter();

	static void sortHunkList(HunkList* hunklist, Transform& transform, ModelList& codeModels, ModelList& dataModels, int baseprobs[8], int numIterations, ProgressBar* progress);
};

#endif