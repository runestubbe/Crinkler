#pragma once
#ifndef _HEURISTIC_HUNK_SORTER_H_
#define _HEURISTIC_HUNK_SORTER_H_

class HunkList;
class HeuristicHunkSorter {
public:
	static void sortHunkList(HunkList* hunklist);
};

#endif