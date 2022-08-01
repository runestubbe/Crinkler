#pragma once
#ifndef _HEURISTIC_HUNK_SORTER_H_
#define _HEURISTIC_HUNK_SORTER_H_

class PartList;
class HeuristicHunkSorter {
public:
	static void SortHunkList(PartList& parts);
};

#endif