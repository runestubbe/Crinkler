#pragma once
#ifndef _HUNK_LIST_H_
#define _HUNK_LIST_H_

#include <vector>
#include <list>

class Hunk;
class Symbol;
class HunkList {
	std::vector<Hunk*>	m_hunks;
public:
	HunkList();
	~HunkList();

	Hunk*& operator[] (unsigned idx);
	Hunk* const & operator[] (unsigned idx) const;

	void addHunkBack(Hunk* hunk);
	void addHunkFront(Hunk* hunk);
	Hunk* removeHunk(Hunk* hunk);
	void append(HunkList* hunklist);
	void setHunk(int index, Hunk* h);
	int getNumHunks() const;
	Hunk* toHunk(const char* name, int* splittingPoint = NULL) const;
	void insertHunk(int index, Hunk* hunk);

	Symbol* findSymbol(const char* name) const;
	Symbol* findUndecoratedSymbol(const char* name) const;
	void removeUnreferencedHunks(std::list<Hunk*> startHunks);

	void removeImportHunks();
	void clear();

	void trim();
	void printHunks();
	void truncateFloats(int defaultBits);
};

#endif
