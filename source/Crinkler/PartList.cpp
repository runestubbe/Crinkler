#include "PartList.h"

#include <cassert>
#include <stack>
#include <algorithm>

#include "Hunk.h"
#include "Log.h"
#include "misc.h"
#include "Symbol.h"

using namespace std;

Part::~Part() {}

Hunk*& Part::operator[] (unsigned idx) {
	return m_hunks[idx];
}

Hunk* const & Part::operator[] (unsigned idx) const {
	return m_hunks[idx];
}

void Part::AddHunkBack(Hunk* hunk) {
	m_hunks.push_back(hunk);
}

void Part::AddHunkFront(Hunk* hunk) {
	m_hunks.insert(m_hunks.begin(), hunk);
}

void Part::InsertHunk(int index, Hunk* hunk) {
	m_hunks.insert(m_hunks.begin() + index, hunk);
}

void Part::RemoveHunk(Hunk* hunk) {
	vector<Hunk*>::iterator it = find(m_hunks.begin(), m_hunks.end(), hunk);
	if(it != m_hunks.end())
		m_hunks.erase(it);
}

void Part::ForEachHunk(std::function<void(Hunk*)> fun)
{
	for(Hunk* hunk : m_hunks)
		fun(hunk);
}

void Part::ForEachHunk(std::function<void(Part&, Hunk*, Hunk*)> fun)
{
	for (auto it = m_hunks.begin(); it != m_hunks.end(); it++)
	{
		Hunk* hunk = *it;
		Hunk* next = (it + 1) != m_hunks.end() ? *(it + 1) : nullptr;
		fun(*this, hunk, next);
	}
}

bool Part::ForEachHunkWithBreak(std::function<bool(Hunk*)> fun)
{
	for (Hunk* hunk : m_hunks)
		if (fun(hunk))
			return true;
	return false;
}

bool Part::ForEachHunkWithBreak(std::function<bool(Part&,Hunk*,Hunk*)> fun)
{
	for(auto it = m_hunks.begin(); it != m_hunks.end(); it++)
	{
		Hunk* hunk = *it;
		Hunk* next = (it + 1) != m_hunks.end() ? *(it + 1) : nullptr;
		if (fun(*this, hunk, next))
			return true;
	}

	return false;
}

void Part::RemoveMatchingHunks(std::function<bool(Hunk*)> fun)
{
	for (vector<Hunk*>::iterator it = m_hunks.begin(); it != m_hunks.end();) {
		if (fun(*it)) {
			delete* it;
			it = m_hunks.erase(it);
		} else {
			it++;
		}
	}
}

PartList::PartList()
{
	m_parts.push_back(new Part("Code", true));
	m_parts.push_back(new Part("Data", true));
	m_parts.push_back(new Part("Uninitialized", false));
}

PartList::~PartList()
{
	for (Part* part : m_parts)
		delete part;
}

void PartList::Clear()
{
	for (Part* part : m_parts)
		delete part;
	m_parts.clear();
}

Part& PartList::operator[] (unsigned idx)
{
	return *m_parts[idx];
}
Part const& PartList::operator[] (unsigned idx) const
{
	return *m_parts[idx];
}

Part& PartList::GetOrAddPart(const char* name, bool initialized)
{
	for (Part* part : m_parts)
	{
		if (part->m_name == name)
		{
			return *part;
		}
	}

	Part* part = new Part(name, initialized);
	if (!m_parts.empty() && !m_parts.back()->m_initialized) {
		m_parts.insert(m_parts.begin() + m_parts.size() - 1, part);
	} else {
		m_parts.push_back(part);
	}

	return *part;
}

void PartList::ForEachHunk(std::function<void(Hunk*)> fun)
{
	for (Part* part : m_parts)
		part->ForEachHunk(fun);
}

void PartList::ForEachHunk(std::function<void(Part&, Hunk*, Hunk*)> fun)
{
	for (Part* part : m_parts)
		part->ForEachHunk(fun);
}

bool PartList::ForEachHunkWithBreak(std::function<bool(Hunk*)> fun)
{
	for (Part* part : m_parts)
		if (part->ForEachHunkWithBreak(fun))
			return true;
	return false;
}

bool PartList::ForEachHunkWithBreak(std::function<bool(Part&, Hunk*, Hunk*)> fun)
{
	for (Part* part : m_parts)
		if (part->ForEachHunkWithBreak(fun))
			return true;
	return false;
}

void PartList::ForEachPart(std::function<void(Part&, int)> fun)
{
	int index = 0;
	for (Part* part : m_parts)
	{
		fun(*part, index++);
	}
}

void PartList::ForEachPart(std::function<void(const Part&, int)> fun) const
{
	int index = 0;
	for (const Part* part : m_parts)
	{
		fun(*part, index++);
	}
}

void PartList::RemoveMatchingHunks(std::function<bool(Hunk*)> fun)
{
	for (Part* part : m_parts)
		part->RemoveMatchingHunks(fun);
}

void PartList::RemoveHunk(Hunk* hunk) {
	for (Part* part : m_parts)
		part->RemoveHunk(hunk);
}

bool PartList::NeedsContinuationJump(Hunk* hunk, Hunk* nextHunk) const {
	Symbol *cont = hunk->GetContinuation();
	if (cont != NULL) {		
		// Continuation symbol is not at the start of the next hunk
		return cont->value > 0 || nextHunk == nullptr || nextHunk != cont->hunk;
	}
	return false;
}

Hunk* PartList::Link(const char* name, int baseAddress) {
	// Calculate raw size
	int rawsize = 0;
	int virtualsize = 0;
	int alignmentBits = 0;
	unsigned int flags = 0;
	bool overflow = false;

	//for(vector<Hunk*>::const_iterator it = m_hunks.begin(); it != m_hunks.end(); it++) {
	ForEachHunkWithBreak([this, baseAddress, &rawsize, &virtualsize, &alignmentBits, &flags, &overflow](Part& part, Hunk* hunk, Hunk* nextHunk) {
		// Align
		virtualsize += baseAddress - hunk->GetAlignmentOffset();
		virtualsize = Align(virtualsize, hunk->GetAlignmentBits());
		virtualsize -= baseAddress - hunk->GetAlignmentOffset();
		if (virtualsize < 0) { overflow = true; return true; }

		// Section contents
		if(hunk->GetRawSize() > 0)
			rawsize = virtualsize + hunk->GetRawSize();
		virtualsize += hunk->GetVirtualSize();
		if (virtualsize < 0) { overflow = true; return true; }
		
		if (NeedsContinuationJump(hunk, nextHunk)) {	// TODO: handle continuation
			rawsize += 5;
			virtualsize = rawsize;
		}
		if (virtualsize < 0) { overflow = true; return true; }

		// Max alignment and flags
		alignmentBits = max(alignmentBits, hunk->GetAlignmentBits());
		if(hunk->GetFlags() & HUNK_IS_CODE)
			flags |= HUNK_IS_CODE;
		if(hunk->GetFlags() & HUNK_IS_WRITEABLE)
			flags |= HUNK_IS_WRITEABLE;
		return false;
	});

	if (overflow) {
		Log::Error("", "Virtual size overflows 2GB limit");
	}

	// Copy data
	Hunk* newHunk = new Hunk(name, 0, flags, alignmentBits, rawsize, virtualsize);
	int address = 0;
	int numContinuationJumps = 0;

	Part* prevPart = nullptr;
	ForEachHunk([&](Part& part, Hunk* hunk, Hunk* nextHunk) {
		// Align
		const int unalignedAddress = address;
		address += baseAddress - hunk->GetAlignmentOffset();
		address = Align(address, hunk->GetAlignmentBits());
		address -= baseAddress - hunk->GetAlignmentOffset();

		if (prevPart != &part) {
			part.m_linkedOffset = address;

			if (prevPart)
				prevPart->m_linkedSize = address - prevPart->m_linkedOffset;
			prevPart = &part;
		}

		// Copy symbols
		for(const auto& p : hunk->m_symbols) {
			Symbol* s = new Symbol(*p.second);
			s->hunk = newHunk;
			if(s->flags & SYMBOL_IS_RELOCATEABLE) {
				s->value += address;
				s->hunkOffset = p.second->hunkOffset + address;
			}
			if (hunk->GetFlags() & HUNK_IS_CODE)
				s->flags |= SYMBOL_IS_CODE;
			newHunk->AddSymbol(s);
		}

		// Copy relocations
		for(Relocation relocation : hunk->m_relocations) {
			relocation.offset += address;
			newHunk->AddRelocation(relocation);
		}

		memcpy(&newHunk->GetPtr()[address], hunk->GetPtr(), hunk->GetRawSize());
		address += hunk->GetVirtualSize();
		
		if (NeedsContinuationJump(hunk, nextHunk)) {
			unsigned char jumpCode[5] = {0xE9, 0x00, 0x00, 0x00, 0x00};
			memcpy(&newHunk->GetPtr()[address], jumpCode, 5);
			
			Relocation r = {hunk->GetContinuation()->name.c_str(), address+1, RELOCTYPE_REL32};
			newHunk->AddRelocation(r);
			
			char symbolName[512];
			sprintf(symbolName, "ContinuationJump%d", numContinuationJumps++);
			Symbol* s = new Symbol(symbolName, address, SYMBOL_IS_RELOCATEABLE | SYMBOL_IS_SECTION | SYMBOL_IS_LOCAL | SYMBOL_IS_CODE, newHunk);
			newHunk->AddSymbol(s);

			address += 5;
		}
	});

	
	if (prevPart)
		prevPart->m_linkedSize = address - prevPart->m_linkedOffset;

	// Trim hunk
	newHunk->Trim();
	
	// Trim parts
	{
		int prevOffset = 0;
		for (Part* part : m_parts) {
			if (part->IsInitialized()) {
				int end = std::min(part->GetLinkedOffset() + part->GetLinkedSize(), newHunk->GetRawSize());
				part->m_linkedSize = std::max(end - part->GetLinkedOffset(), 0);
				prevOffset = part->GetLinkedOffset() + part->GetLinkedSize();
			}
			else
			{
				int end = part->GetLinkedOffset() + part->GetLinkedSize();
				part->m_linkedOffset = prevOffset;
				part->m_linkedSize = end - prevOffset;
				prevOffset = end;
			}
		}
	}

	return newHunk;
}

Symbol* PartList::FindUndecoratedSymbol(const char* name) {
	// Weak libs (0) < weak (1) < libs (2) < normal (3)
	int best_level = -1;
	Symbol* res = NULL;

	ForEachHunk([&](Hunk* hunk)
	{
		Symbol* s = hunk->FindUndecoratedSymbol(name);
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
		return true;
	});

	return res;
}

Symbol* PartList::FindSymbol(const char* name) {
	Symbol* res = NULL;
	ForEachHunk([name, &res](Hunk* hunk) {
		Symbol* s = hunk->FindSymbol(name);
		if(s != NULL) {
			res = s;
			if(s->secondaryName.size() == 0)
				return false;
		}
		return true;
	});

	return res;
}

void PartList::RemoveUnreferencedHunks(vector<Hunk*> startHunks) {
	stack<Hunk*> stak;
	for(Hunk* hunk : startHunks) {
		hunk->m_numReferences++;
		stak.push(hunk);
	}

	// Mark reachable hunks
	while(stak.size() > 0) {
		Hunk* h = stak.top();
		stak.pop();

		for(Relocation& relocation : h->m_relocations) {
			Symbol* s = FindSymbol(relocation.symbolname.c_str());
			
			if(s) {
				if(s->secondaryName.size() > 0)	{	// Weak symbol
					s->hunk->m_numReferences++;
					s = FindSymbol(s->secondaryName.c_str());
					if(s == NULL)
						continue;
				}
				if(s->hunk->m_numReferences++ == 0) {
					stak.push(s->hunk);
				}
			}
		}
	}

	// Delete unreferenced hunks
	RemoveMatchingHunks([&](Hunk* hunk) {
		return hunk->GetNumReferences() == 0;
		});
	
}