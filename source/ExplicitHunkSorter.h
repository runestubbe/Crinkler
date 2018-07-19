#pragma once

class HunkList;
class Reuse;
class ExplicitHunkSorter {
public:
	static void sortHunkList(HunkList* hunklist, Reuse *reuse);
};
