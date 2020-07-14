#pragma once

class HunkList;
class Reuse;
class ExplicitHunkSorter {
public:
	static void SortHunkList(HunkList* hunklist, Reuse *reuse);
};
