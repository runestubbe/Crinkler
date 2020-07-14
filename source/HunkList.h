#pragma once
#ifndef _HUNK_LIST_H_
#define _HUNK_LIST_H_

#include <vector>

class Hunk;
class Symbol;
class HunkList {
	std::vector<Hunk*>	m_hunks;
public:
	HunkList();
	~HunkList();

	Hunk*& operator[] (unsigned idx);
	Hunk* const & operator[] (unsigned idx) const;

	void	AddHunkBack(Hunk* hunk);
	void	AddHunkFront(Hunk* hunk);
	Hunk*	RemoveHunk(Hunk* hunk);
	void	Append(HunkList* hunklist);
	int		GetNumHunks() const;
	Hunk*	ToHunk(const char* name, int base_address = 0, int* splittingPoint = NULL) const;
	void	InsertHunk(int index, Hunk* hunk);

	Symbol* FindSymbol(const char* name) const;
	Symbol* FindUndecoratedSymbol(const char* name) const;
	void	RemoveUnreferencedHunks(std::vector<Hunk*> startHunks);

	bool	NeedsContinuationJump(std::vector<Hunk*>::const_iterator &it) const;

	void	RemoveImportHunks();
	void	Clear();

	void	MarkHunksAsLibrary();

	void	Trim();
	void	PrintHunks();
	void	RoundFloats(int defaultBits);
};

#endif
