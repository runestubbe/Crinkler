#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>
#include "misc.h"

#include "Hunk.h"
#include "NameMangling.h"
#include "Log.h"
#include "Symbol.h"

using namespace std;

Hunk::Hunk(const Hunk& h) : 
	m_alignmentBits(h.m_alignmentBits), m_flags(h.m_flags), m_data(h.m_data),
	m_virtualsize(h.m_virtualsize), m_relocations(h.m_relocations), m_name(h.m_name),
	m_importName(h.m_importName), m_importDll(h.m_importDll), m_numReferences(0)
{
	//deep copy symbols
	for(map<std::string, Symbol*>::const_iterator it = h.m_symbols.begin(); it != h.m_symbols.end(); it++) {
		Symbol* s = new Symbol(*it->second);
		s->hunk = this;
		m_symbols[it->second->name] = s;
	}
}


Hunk::Hunk(const char* symbolName, const char* importName, const char* importDll) :
	m_name(symbolName), m_virtualsize(0),
	m_flags(HUNK_IS_IMPORT), m_alignmentBits(0), m_importName(importName),
	m_importDll(importDll), m_numReferences(0)
{
	addSymbol(new Symbol(symbolName, 0, SYMBOL_IS_RELOCATEABLE, this));
}


Hunk::Hunk(const char* name, const char* data, unsigned int flags, int alignmentBits, int rawsize, int virtualsize) :
	m_name(name), m_flags(flags), m_alignmentBits(alignmentBits),
	m_virtualsize(virtualsize), m_numReferences(0)
{
	m_data.resize(rawsize);
	if(data != NULL)
		copy(data, data+rawsize, m_data.begin());
}


Hunk::~Hunk() {
	//free symbols
	for(map<std::string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		delete it->second;
	}
}

void Hunk::addSymbol(Symbol* s) {
	map<string, Symbol*>::iterator it = m_symbols.find(s->name.c_str());
	if(it == m_symbols.end()) {
		m_symbols.insert(make_pair(s->name, s));
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
	assert(r.offset <= getRawSize()-4);
	m_relocations.push_back(r);
}

const char* Hunk::getName() const {
	return m_name.c_str();
}

//sort symbols by address, then by type (section < global < local) and finally by name
static bool symbolComparator(Symbol*& first, Symbol*& second) {
	if(first->value != second->value)
		return first->value < second->value;
	else
		if((first->flags & SYMBOL_IS_SECTION) || (second->flags & SYMBOL_IS_SECTION))
			return first->flags & SYMBOL_IS_SECTION;
		if((first->flags & SYMBOL_IS_LOCAL) || (second->flags & SYMBOL_IS_LOCAL))
			return !(first->flags & SYMBOL_IS_LOCAL);
		return first->name < second->name;
}


void Hunk::printSymbols() const {
	vector<Symbol*> symbols;
	//extract relocatable symbols
	for(map<string, Symbol*>::const_iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		if(it->second->flags & SYMBOL_IS_RELOCATEABLE)
			symbols.push_back(it->second);
	}

	//sort symbol by value
	sort(symbols.begin(), symbols.end(), symbolComparator);

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
	if(!m_data.empty())
		return &m_data[0];
	else
		return NULL;
}

int Hunk::getRawSize() const {
	return m_data.size();
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
	int farestReloc = 0;
	for(list<relocation>::const_iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		int relocSize = 4;
		farestReloc = max(it->offset+relocSize, farestReloc);
	}

	while(m_data.size() > farestReloc && m_data.back() == 0)
		m_data.pop_back();
}

//chop of x bytes from the initialized data
void Hunk::chop(int size) {
	m_data.erase((m_data.end()-size), m_data.end());
}

CompressionReportRecord* Hunk::getCompressionSummary(int* sizefill, int splittingPoint) {
	vector<Symbol*> symbols;
	//extract relocatable symbols
	for(map<string, Symbol*>::const_iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		if(it->second->flags & SYMBOL_IS_RELOCATEABLE)
			symbols.push_back(it->second);
	}

	//sort symbols
	sort(symbols.begin(), symbols.end(), symbolComparator);

	CompressionReportRecord* root = new CompressionReportRecord("root", RECORD_ROOT, 0, 0);
	CompressionReportRecord* codeSection = new CompressionReportRecord("Code section", RECORD_SECTION|RECORD_CODE, 0, 0);
	CompressionReportRecord* dataSection = new CompressionReportRecord("Data section", RECORD_SECTION, splittingPoint, sizefill[splittingPoint]);
	CompressionReportRecord* uninitSection = new CompressionReportRecord("Uninitialized section", RECORD_SECTION, getRawSize(), -1);
	root->children.push_back(codeSection);
	root->children.push_back(dataSection);
	root->children.push_back(uninitSection);

	for(vector<Symbol*>::iterator it = symbols.begin(); it != symbols.end(); it++) {
		CompressionReportRecord* c = new CompressionReportRecord((*it)->name.c_str(), 
			0, (*it)->value, ((*it)->value < getRawSize()) ? sizefill[(*it)->value] : -1);

		//copy misc string
		c->miscString = (*it)->miscString;

		//set flags
		if((*it)->flags & SYMBOL_IS_SECTION) {
			c->type |= RECORD_OLD_SECTION;
		}
		if(!((*it)->flags & SYMBOL_IS_LOCAL)) {
			c->type |= RECORD_PUBLIC;
		}

		//find appropriate section
		CompressionReportRecord* r;
		if((*it)->value < splittingPoint) {
			r = codeSection;
		} else if((*it)->value < getRawSize()) {
			r = dataSection;
		} else {
			r = uninitSection;
		}
		//find where to place the record
		while(r->children.size() > 0 && r->children.back()->getLevel() < c->getLevel())
			r = r->children.back();

		r->children.push_back(c);

/*
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
		} else if((*it)->value < getRawSize()) {
			dataSection->children.push_back(c);
		} else {
			uninitSection->children.push_back(c);
		}
		*/
	}

	root->calculateSize(m_virtualsize, sizefill[getRawSize()]);
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
		offsetmap.insert(make_pair(it->offset, s));
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

			offsetmap.insert(make_pair(s->value, s));
		}
	}
	return offsetmap;
}

void Hunk::roundFloats(int defaultBits) {
	for(map<string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		Symbol* s = it->second;
		int bits = defaultBits;
		string name = undecorateSymbolName(s->name.c_str());

		//REMARK: a float with most significant byte = 0 could be trimmed down
		//and thus not get truncated. Just ignore it. It only happens to numbers with norm < 10^-38
		if(s->value < 0 | s->value > getRawSize()-4)	//sanity check.
			continue;

		//round __real@XXXXXXXX to default bits
		//round tfXX_?? to XX bits
		int a;
		if(sscanf_s(s->name.c_str(), "__real@%8X", &a) && s->name.size() == (2+4+1+8)) {
			bits = defaultBits;
		} else if(sscanf_s(name.c_str(), "tf%d_", &bits) && bits >= 0 && bits <= 32) {
			
		} else {
			continue;
		}
		
		float orgv = *(float*)&m_data[s->value];
		float v = roundFloat(orgv, bits);
		*(float*)&m_data[s->value] = v;
		printf("symbol %s rounded to %d bits: %f -> %f (%.8X)\n", stripCrinklerSymbolPrefix(s->name.c_str()).c_str(), bits, orgv, v, *(unsigned int*)&v);
	}
}