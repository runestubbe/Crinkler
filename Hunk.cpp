#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>

#include "Hunk.h"
#include "NameMangling.h"
#include "Log.h"
#include "Symbol.h"

using namespace std;

Hunk::Hunk(const Hunk& h) : 
	m_alignmentBits(h.m_alignmentBits), m_flags(h.m_flags), m_rawsize(h.m_rawsize),
	m_virtualsize(h.m_virtualsize), m_relocations(h.m_relocations), m_name(h.m_name),
	m_importName(h.m_importName), m_importDll(h.m_importDll), m_numReferences(0)
{
	if(m_rawsize > 0) {
		m_data = new char[m_rawsize];
		memcpy(m_data, h.m_data, m_rawsize);
	} else {
		m_data = NULL;
	}

	//deep copy symbols
	for(map<std::string, Symbol*>::const_iterator it = h.m_symbols.begin(); it != h.m_symbols.end(); it++) {
		Symbol* s = new Symbol(*it->second);
		s->hunk = this;
		m_symbols[it->second->name] = s;
	}
}


Hunk::Hunk(const char* symbolName, const char* importName, const char* importDll) :
	m_name(symbolName), m_data(NULL), m_rawsize(0), m_virtualsize(0),
	m_flags(HUNK_IS_IMPORT), m_alignmentBits(0), m_importName(importName),
	m_importDll(importDll), m_numReferences(0)
{
	addSymbol(new Symbol(symbolName, 0, SYMBOL_IS_RELOCATEABLE, this));
}


Hunk::Hunk(const char* name, const char* data, unsigned int flags, int alignmentBits, int rawsize, int virtualsize) :
	m_name(name), m_flags(flags), m_alignmentBits(alignmentBits),
	m_rawsize(rawsize), m_virtualsize(virtualsize), m_numReferences(0)
{
	if(rawsize > 0) {
		m_data = new char[rawsize];
		if(data != 0)
			memcpy(m_data, data, rawsize);
		else
			memset(m_data, 0, rawsize);
	} else {
		m_data = 0;
	}
}


Hunk::~Hunk() {
	//free data
	delete[] m_data;

	//free symbols
	for(map<std::string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		delete it->second;
	}
}

void Hunk::addSymbol(Symbol* s) {
	map<string, Symbol*>::iterator it = m_symbols.find(s->name.c_str());
	if(it == m_symbols.end()) {
		pair<string, Symbol*> p(s->name, s);
		m_symbols.insert(p);
	} else {
		Symbol* oldSym = it->second;
		if(oldSym->secondaryName.size() > 0) {
			//overwrite weak symbols
			m_symbols[s->name.c_str()] = s;
			delete oldSym;
		} else {
			delete s;
		}
	}
}

void Hunk::addRelocation(relocation r) {
	assert(r.offset >= 0);
	assert(r.offset <= m_rawsize-4);
	m_relocations.push_back(r);
}

const char* Hunk::getName() const {
	return m_name.c_str();
}

struct SymbolComparator {
	bool operator()(Symbol*& first, Symbol*& second) {
		if(first->value != second->value)
			return first->value < second->value;
		else
			return first->flags & SYMBOL_IS_SECTION;
	}
};

void Hunk::printSymbols() const {
	vector<Symbol*> symbols;
	//extract relocatable symbols
	for(map<string, Symbol*>::const_iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		if(it->second->flags & SYMBOL_IS_RELOCATEABLE)
			symbols.push_back(it->second);
	}

	//sort symbol by value
	sort(symbols.begin(), symbols.end(), SymbolComparator());

	//print symbol ordered symbols
	for(vector<Symbol*>::const_iterator it = symbols.begin(); it != symbols.end(); it++) {
		printf("%8x: %s\n", (*it)->value, (*it)->name.c_str());
	}
}


unsigned int Hunk::getFlags() const {
	return m_flags;
}

Symbol* Hunk::findUndecoratedSymbol(const char* name) const {
	for(map<string, Symbol*>::const_iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		Symbol* s = it->second;
		if(undecorateSymbolName(s->name.c_str()).compare(name) == 0)
			return s;
	}
	return NULL;
}

Symbol* Hunk::findSymbol(const char* name) const {
	map<string, Symbol*>::const_iterator it = m_symbols.find(name);
	if(it != m_symbols.end())
		return it->second;
	else
		return NULL;
}

void Hunk::fixate() {
	m_flags |= HUNK_IS_FIXED;
}

void Hunk::relocate(int imageBase) {
	for(list<relocation>::const_iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		relocation r = *it;

		//find symbol
		Symbol* s = findSymbol(r.symbolname.c_str());
		if(s && s->secondaryName.size() > 0)
			s = findSymbol(s->secondaryName.c_str());
		if(s == NULL) {
			Log::error(0, "", "could not find symbol '%s'\n", r.symbolname.c_str());
		}

		//perform relocation
		int* word = (int*)&m_data[r.offset];
		switch(r.type) {
			case RELOCTYPE_ABS32:
				(*word) += s->value;
				if(s->flags & SYMBOL_IS_RELOCATEABLE)
					(*word) += imageBase;
				break;
			case RELOCTYPE_REL32:
				(*word) += s->value - r.offset - 4;
				break;
		}
	}
}

int Hunk::getAlignmentBits() const {
	return m_alignmentBits;
}

char* Hunk::getPtr() {
	return m_data;
}

int Hunk::getRawSize() const {
	return m_rawsize;
}

int Hunk::getVirtualSize() const {
	return m_virtualsize;
}

int Hunk::getNumReferences() const {
	return m_numReferences;
}

const char* Hunk::getImportName() const {
	return m_importName.c_str();
}

const char* Hunk::getImportDll() const {
	return m_importDll.c_str();
}

void Hunk::setVirtualSize(int size) {
	m_virtualsize = size;
}

void Hunk::setAlignmentBits(int alignmentBits) {
	m_alignmentBits = alignmentBits;
}

void Hunk::trim() {
	int oldsize = m_rawsize;
	int farestReloc = 0;
	for(list<relocation>::const_iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		int relocSize = 4;
		farestReloc = max(it->offset+relocSize, farestReloc);
	}

	while(m_rawsize > farestReloc && m_data[m_rawsize-1] == 0)
		m_rawsize--;

	//realloc memory
	if(m_rawsize != oldsize) {
		if(m_rawsize > 0) {
			char* data = new char[m_rawsize];
			memcpy(data, m_data, m_rawsize);
			delete[] m_data;
			m_data = data;
		} else {
			delete[] m_data;
			m_data = NULL;
		}
	}
}

//chop of x bytes from the initialized data
void Hunk::chop(int size) {
	assert(m_rawsize >= size);
	m_rawsize -= size;
}

CompressionSummaryRecord* Hunk::getCompressionSummary(int* sizefill, int splittingPoint) {
	vector<Symbol*> symbols;
	//extract relocatable symbols
	for(map<string, Symbol*>::const_iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		if(it->second->flags & SYMBOL_IS_RELOCATEABLE)
			symbols.push_back(it->second);
	}

	//sort symbol by value
	sort(symbols.begin(), symbols.end(), SymbolComparator());

	CompressionSummaryRecord* root = new CompressionSummaryRecord("root", RECORD_ROOT, 0, 0);
	CompressionSummaryRecord* codeSection = new CompressionSummaryRecord("Code section", RECORD_SECTION, 0, 0);
	CompressionSummaryRecord* dataSection = new CompressionSummaryRecord("Data section", RECORD_SECTION, splittingPoint, sizefill[splittingPoint]);
	CompressionSummaryRecord* uninitSection = new CompressionSummaryRecord("Uninitialized section", RECORD_SECTION, m_rawsize, -1);
	root->children.push_back(codeSection);
	root->children.push_back(dataSection);
	root->children.push_back(uninitSection);

	for(vector<Symbol*>::iterator it = symbols.begin(); it != symbols.end(); it++) {
		CompressionSummaryRecord* c = new CompressionSummaryRecord(stripCrinklerSymbolPrefix((*it)->name.c_str()).c_str(), 
			((*it)->flags & SYMBOL_IS_LOCAL) ? 0 : RECORD_PUBLIC, (*it)->value, ((*it)->value < m_rawsize) ? sizefill[(*it)->value] : -1);

		c->miscString = (*it)->miscString;
		if((*it)->flags & SYMBOL_IS_FUNCTION) {
			c->miscString = (*it)->getUndecoratedName();
			c->functionSize = (*it)->size;
			c->compressedFunctionSize = sizefill[(*it)->value+c->functionSize] - sizefill[(*it)->value];
			c->type |= RECORD_FUNCTION;
		}

		if((*it)->flags & SYMBOL_IS_SECTION)
			c->type |= RECORD_OLD_SECTION;

		if((*it)->value < splittingPoint) {
			codeSection->children.push_back(c);
		} else if((*it)->value < m_rawsize) {
			dataSection->children.push_back(c);
		} else {
			uninitSection->children.push_back(c);
		}
	}

	root->calculateSize(m_virtualsize, sizefill[m_rawsize]);
	return root;
}


void Hunk::setImportDll(const char* dll) {
	m_importDll = dll;
}

map<int, Symbol*> Hunk::getOffsetToRelocationMap() {
	map<int, Symbol*> offsetmap;
	map<int, Symbol*> symbolmap = getOffsetToSymbolMap();
	for(list<relocation>::iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		Symbol* s = findSymbol(it->symbolname.c_str());
		if(s && s->secondaryName.size() > 0)
			s = findSymbol(s->secondaryName.c_str());
		if(s->flags & SYMBOL_IS_RELOCATEABLE && s->flags & SYMBOL_IS_SECTION)
			s = symbolmap.find(s->value)->second;	//replace relocation to section with non-section
		pair<int, Symbol*> p(it->offset, s);
		offsetmap.insert(p);
	}
	return offsetmap;
}

map<int, Symbol*> Hunk::getOffsetToSymbolMap() {
	map<int, Symbol*> offsetmap;
	for(map<string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		Symbol* s = it->second;
		if(s->flags & SYMBOL_IS_RELOCATEABLE) {
			if(offsetmap.find(s->value) != offsetmap.end() && s->flags & SYMBOL_IS_SECTION)	//favor non-sections
				continue;

			pair<int, Symbol*> p(s->value, s);
			offsetmap.insert(p);
		}
	}
	return offsetmap;
}

static float truncateFloat(float ptr, int bits) {
	int truncBits = 32-bits;
	float* v = &ptr;
	int v1;
	double orgv = *v;
	unsigned int* iv = (unsigned int*)v;

	if(bits < 2) {
		*iv = 0;
	} else {
		//round
		//compare actual floating point values due to non-linearity
		*iv >>= truncBits;
		*iv <<= truncBits;
		if(truncBits > 0) {
			double v0 = *v;
			(*iv) += 1<<truncBits;
			double v1 = *v;
			if(abs(orgv-v0) < abs(orgv-v1)) {
				(*iv) -= 1<<truncBits;
			}
		}
	}
	return *v;
}

void Hunk::truncateFloats(int defaultBits) {
	for(map<string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		Symbol* s = it->second;
		int bits = defaultBits;
		string name = undecorateSymbolName(s->name.c_str());

		//REMARK: a float with most significant byte = 0 could be trimmed down
		//and thus not get truncated. Just ignore it. It only happens to numbers with norm < 10^-38
		if(s->value < 0 | s->value > m_rawsize-4)	//sanity check.
			continue;

		int a;
		if(sscanf_s(s->name.c_str(), "__real@%8X", &a) && s->name.size() == (2+4+1+8)) {	//TODO: fix hacked check
			bits = defaultBits;
		} else if(sscanf_s(name.c_str(), "tf%d_", &bits) && bits >= 0 && bits <= 32) {
			
		} else {
			continue;
		}
		
		float orgv = *(float*)&m_data[s->value];
		float v = truncateFloat(orgv, bits);			
		*(float*)&m_data[s->value] = v;
		printf("symbol %s truncated to %d bits: %f -> %f (%.8X)\n", stripCrinklerSymbolPrefix(s->name.c_str()).c_str(), bits, orgv, v, *(unsigned int*)&v);
	}
}