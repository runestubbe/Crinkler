#pragma once

class PartList;
class Reuse;
class ExplicitHunkSorter {
public:
	static void SortHunks(PartList& parts, Reuse *reuse);
};
