#ifndef _EMPIRICAL_HUNK_SORTER_H_
#define _EMPIRICAL_HUNK_SORTER_H_

class HunkList;
class ModelList;
class ProgressBar;
class EmpiricalHunkSorter {
	static int tryHunkCombination(HunkList* hunklist, ModelList& codeModels, ModelList& dataModels, int baseprobs[8]);
public:
	EmpiricalHunkSorter();
	~EmpiricalHunkSorter();

	static void sortHunkList(HunkList* hunklist, ModelList& codeModels, ModelList& dataModels, int baseprobs[8], int numIterations, ProgressBar* progress);
};

#endif