#pragma once
#ifndef _EMPIRICAL_HUNK_SORTER_H_
#define _EMPIRICAL_HUNK_SORTER_H_

class HunkList;
class ModelList;
class ProgressBar;
class Transform;
class EmpiricalHunkSorter {
	static int tryHunkCombination(HunkList* hunklist, Transform& transform, ModelList& codeModels, ModelList& dataModels, int baseprob);
public:
	EmpiricalHunkSorter();
	~EmpiricalHunkSorter();

	static void sortHunkList(HunkList* hunklist, Transform& transform, ModelList& codeModels, ModelList& dataModels, int baseprob, int numIterations, ProgressBar* progress);
};

#endif