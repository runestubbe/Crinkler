#pragma once
#ifndef _EMPIRICAL_HUNK_SORTER_H_
#define _EMPIRICAL_HUNK_SORTER_H_

class PartList;
class ModelList4k;
class ModelList1k;
class ProgressBar;
class Transform;
class EmpiricalHunkSorter {
	static int TryHunkCombination(PartList& parts, Transform& transform, bool saturate, bool use1KMode);
public:
	static int SortHunkList(PartList& parts, Transform& transform, bool saturate, int numIterations, ProgressBar* progress, bool use1KMode);
};

#endif