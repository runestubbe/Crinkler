#include "HunkList.h"

#include <cassert>
#include <stack>
#include <algorithm>

#include "Hunk.h"
#include "Log.h"
#include "misc.h"
#include "Symbol.h"

using namespace std;

HunkList::HunkList() {
}

HunkList::~HunkList() {
	//free hunks;
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end(); it++) {
		delete *it;
	}
}

Hunk*& HunkList::operator[] (unsigned idx) {
	assert(idx < (int)m_hunks.size());
	return m_hunks[idx];
}

Hunk* const & HunkList::operator[] (unsigned idx) const {
	assert(idx < (int)m_hunks.size());
	return m_hunks[idx];
}

void HunkList::addHunkBack(Hunk* hunk) {
	m_hunks.push_back(hunk);
}

void HunkList::addHunkFront(Hunk* hunk) {
	m_hunks.insert(m_hunks.begin(), hunk);
}

void HunkList::insertHunk(int index, Hunk* hunk) {
	m_hunks.insert(m_hunks.begin() + index, hunk);
}

Hunk* HunkList::removeHunk(Hunk* hunk) {
	vector<Hunk*>::iterator it = find(m_hunks.begin(), m_hunks.end(), hunk);
	if(it != m_hunks.end())
		m_hunks.erase(it);
	return hunk;
}


void HunkList::clear() {
	m_hunks.clear();
}

int HunkList::getNumHunks() const {
	return (int)m_hunks.size();
}

void HunkList::setHunk(int index, Hunk* h) {
	assert(index >= 0 && index < (int)m_hunks.size());
	m_hunks[index] = h;
}


void HunkList::append(HunkList* hunklist) {
	for(vector<Hunk*>::const_iterator it = hunklist->m_hunks.begin(); it != hunklist->m_hunks.end(); it++) {
		m_hunks.push_back(new Hunk(*(*it)));
	}
}

bool HunkList::needsContinuationJump(vector<Hunk*>::const_iterator &it) const {
	Hunk *h = *it;
	Symbol *cont = h->getContinuation();
	if (cont != NULL) {
		vector<Hunk*>::const_iterator next_it = it+1;
		return
			// Continuation symbol is not at the start of the next hunk
			cont->value > 0 || next_it == m_hunks.end() || *next_it != cont->hunk;
	}
	return false;
}


Hunk* HunkList::toHunk(const char* name, int baseAddress, int* splittingPoint) const {
	//calculate raw size
	int rawsize = 0;
	int virtualsize = 0;
	int alignmentBits = 0;
	unsigned int flags = 0;
	for(vector<Hunk*>::const_iterator it = m_hunks.begin(); it != m_hunks.end(); it++) {
		Hunk* h = *it;
		// align
		virtualsize += baseAddress - h->getAlignmentOffset();
		virtualsize = align(virtualsize, h->getAlignmentBits());
		virtualsize -= baseAddress - h->getAlignmentOffset();

		// section contents
		if(h->getRawSize() > 0)
			rawsize = virtualsize + h->getRawSize();
		virtualsize += h->getVirtualSize();
		if (needsContinuationJump(it)) {
			rawsize += 5;
			virtualsize = rawsize;
		}

		// max alignment and flags
		alignmentBits = max(alignmentBits, h->getAlignmentBits());
		if(h->getFlags() & HUNK_IS_CODE)
			flags |= HUNK_IS_CODE;
		if(h->getFlags() & HUNK_IS_WRITEABLE)
			flags |= HUNK_IS_WRITEABLE;
	}

	//copy data
	Hunk* newHunk = new Hunk(name, 0, flags, alignmentBits, rawsize, virtualsize);
	int address = 0;
	if(splittingPoint != NULL)
		*splittingPoint = -1;

	for(vector<Hunk*>::const_iterator it = m_hunks.begin(); it != m_hunks.end(); it++) {
		Hunk* h = *it;
		// align
		address += baseAddress - h->getAlignmentOffset();
		address = align(address, h->getAlignmentBits());
		address -= baseAddress - h->getAlignmentOffset();

		//copy symbols
		for(map<string, Symbol*>::const_iterator jt = h->m_symbols.begin(); jt != h->m_symbols.end(); jt++) {
			Symbol* s = new Symbol(*jt->second);
			s->hunk = newHunk;
			if(s->flags & SYMBOL_IS_RELOCATEABLE) {
				s->value += address;
			}
			newHunk->addSymbol(s);
		}

		//copy relocations
		for(list<relocation>::const_iterator jt = h->m_relocations.begin(); jt != h->m_relocations.end(); jt++) {
			relocation r = *jt;
			r.offset += address;
			newHunk->addRelocation(r);
		}

		if(splittingPoint && *splittingPoint == -1 && !(h->getFlags() & HUNK_IS_CODE))
			*splittingPoint = address;

		memcpy(&newHunk->getPtr()[address], h->getPtr(), h->getRawSize());
		if (needsContinuationJump(it)) {
			unsigned char jumpCode[5] = {0xE9, 0x00, 0x00, 0x00, 0x00};
			memcpy(&newHunk->getPtr()[address+h->getRawSize()], jumpCode, 5);
			relocation r = {h->getContinuation()->name.c_str(), address+h->getRawSize()+1, RELOCTYPE_REL32};
			newHunk->addRelocation(r);
			address += h->getRawSize()+5;
		} else {
			address += h->getVirtualSize();
		}
	}
	newHunk->trim();

	return newHunk;
}


Symbol* HunkList::findUndecoratedSymbol(const char* name) const {
	//weak libs (0) < weak (1) < libs (2) < normal (3)
	int best_level = -1;
	Symbol* res = NULL;
	for(vector<Hunk*>::const_iterator it = m_hunks.begin(); it != m_hunks.end(); it++) {
		Symbol* s = (*it)->findUndecoratedSymbol(name);
		if(s != NULL) {
			int level = 0;
			if(s->fromLibrary) {
				if(s->secondaryName.empty()) {
					level = 2;
				} else {
					level = 0;
				}
			} else {
				if(s->secondaryName.empty()) {
					level = 3;
				} else {
					level = 1;
				}
			}
			if(level > best_level) {
				best_level = level;
				res = s;
			}
		}
		
	}

	return res;
}

Symbol* HunkList::findSymbol(const char* name) const {
	Symbol* res = NULL;
	for(vector<Hunk*>::const_iterator it = m_hunks.begin(); it != m_hunks.end(); it++) {
		Symbol* s = (*it)->findSymbol(name);
		if(s != NULL) {
			if(s->secondaryName.size() == 0)
				return s;
			else
				res = s;
		}
	}

	return res;
}

void HunkList::removeUnreferencedHunks(list<Hunk*> startHunks) {
	stack<Hunk*> stak;
	for(list<Hunk*>::iterator it = startHunks.begin(); it != startHunks.end(); it++) {
		(*it)->m_numReferences++;
		stak.push(*it);
	}

	//mark reachable hunks
	while(stak.size() > 0) {
		Hunk* h = stak.top();
		stak.pop();

		for(list<relocation>::iterator it = h->m_relocations.begin(); it != h->m_relocations.end(); it++) {
			Symbol* s = findSymbol(it->symbolname.c_str());
			
			if(s) {
				if(s->secondaryName.size() > 0)	{//weak symbol
					s->hunk->m_numReferences++;
					s = findSymbol(s->secondaryName.c_str());
					if(s == NULL)
						continue;
				}
				if(s->hunk->m_numReferences++ == 0) {
					stak.push(s->hunk);
				}
			}
		}
	}

	//delete unreferenced hunks
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end();) {
		if((*it)->getNumReferences() == 0) {
			delete *it;
			it = m_hunks.erase(it);
		} else {
			it++;
		}
	}
}

void HunkList::removeImportHunks() {
	//delete import hunks
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end();) {
		if((*it)->getFlags() & HUNK_IS_IMPORT) {
			delete *it;
			it = m_hunks.erase(it);
		} else {
			it++;
		}
	}
}

void HunkList::trim() {
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end(); it++)
		(*it)->trim();
}

void HunkList::printHunks() {
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end(); it++)
		(*it)->printSymbols();
}

void HunkList::roundFloats(int defaultBits) {
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end(); it++)
		(*it)->roundFloats(defaultBits);
}

void HunkList::markHunksAsLibrary() {
	for(vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end(); it++)
		(*it)->markHunkAsLibrary();
}