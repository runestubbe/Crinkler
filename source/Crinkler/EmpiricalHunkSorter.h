#pragma once
#ifndef _EMPIRICAL_HUNK_SORTER_H_
#define _EMPIRICAL_HUNK_SORTER_H_

class HunkList;
class ModelList4k;
class ModelList1k;
class ProgressBar;
class Transform;
class EmpiricalHunkSorter {
	static int TryHunkCombination(HunkList* hunklist, Transform& transform, ModelList4k& codeModels, ModelList4k& dataModels, ModelList1k& models1k, int baseprob, bool saturate, bool use1KMode, int* out_size1, int* out_size2);
public:
	EmpiricalHunkSorter();
	~EmpiricalHunkSorter();

	static int SortHunkList(HunkList* hunklist, Transform& transform, ModelList4k& codeModels, ModelList4k& dataModels, ModelList1k& models1k, int baseprob, bool saturate, int numIterations, ProgressBar* progress, bool use1KMode, int* out_size1, int* out_size2);
};

#endif