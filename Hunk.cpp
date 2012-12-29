#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>
#include "StringMisc.h"
#include "misc.h"

#include "Hunk.h"
#include "NameMangling.h"
#include "Log.h"
#include "Symbol.h"

using namespace std;

Hunk::Hunk(const Hunk& h) : 
	m_alignmentBits(h.m_alignmentBits), m_flags(h.m_flags), m_data(h.m_data),
	m_virtualsize(h.m_virtualsize), m_relocations(h.m_relocations), m_name(h.m_name),
	m_importName(h.m_importName), m_importDll(h.m_importDll), m_numReferences(0),
	m_alignmentOffset(0), m_continuation(NULL)
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
	m_importDll(importDll), m_numReferences(0),
	m_alignmentOffset(0), m_continuation(NULL)
{
	addSymbol(new Symbol(symbolName, 0, SYMBOL_IS_RELOCATEABLE, this));
}


Hunk::Hunk(const char* name, const char* data, unsigned int flags, int alignmentBits, int rawsize, int virtualsize) :
	m_name(name), m_flags(flags), m_alignmentBits(alignmentBits),
	m_virtualsize(virtualsize), m_numReferences(0),
	m_alignmentOffset(0), m_continuation(NULL)
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

void Hunk::setContinuation(Symbol* s) {
	m_continuation = s;
}

Symbol* Hunk::getContinuation() const {
	return m_continuation;
}

const char* Hunk::getName() const {
	return m_name.c_str();
}

//sort symbols by address, then by type (section < global < local) and finally by name
static bool symbolComparator(Symbol* first, Symbol* second) {
	if(first->value != second->value) {
		return first->value < second->value;
	} else if(first->hunk_offset != second->hunk_offset) {
		return first->hunk_offset < second->hunk_offset;
	} else {
		if((first->flags & SYMBOL_IS_SECTION) != (second->flags & SYMBOL_IS_SECTION))
			return first->flags & SYMBOL_IS_SECTION;
		if((first->flags & SYMBOL_IS_LOCAL) != (second->flags & SYMBOL_IS_LOCAL))
			return !(first->flags & SYMBOL_IS_LOCAL);
		return first->name < second->name;
	}
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

//generates a help message based on the symbol name
static string HelpMessage(const char* name) {
	if(startsWith(name, "__RTC_")) {
		return "Disable 'Basic Runtime Checks' in the compiler options.";
	} else if(startsWith(name, "__security_cookie")) {
		return "Disable 'Buffer Security Check' in the compiler options.";
	} else if(startsWith(name, "_crt_debugger_hook") || startsWith(name, "___ImageBase")) {
		return "Define your own entry point.";
	} else if(startsWith(name, "__ftol")) {
		return "Suppress _ftol calls with the /QIfist compiler option.";
	} else if(startsWith(name, "__Cxx")) {
		return "Don't use exceptions.";
	} else if(startsWith(name, "__alloca") || startsWith(name, "__chkstk")) {
		return "Avoid declaring large arrays or structs inside functions. Use global variables instead.";
	}
	return "";
}

void Hunk::relocate(int imageBase) {
	for(vector<relocation>::const_iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		relocation r = *it;

		//find symbol
		Symbol* s = findSymbol(r.symbolname.c_str());
		if(s && s->secondaryName.size() > 0)
			s = findSymbol(s->secondaryName.c_str());
		if(s == NULL) {
			string help = HelpMessage(r.symbolname.c_str());
			if(help.empty()) {
				Log::error(r.objectname.c_str(), "Cannot find symbol '%s'", r.symbolname.c_str());
			} else {
				Log::error(r.objectname.c_str(), "Cannot find symbol '%s'.\n * HINT: %s", r.symbolname.c_str(), help.c_str());
			}
			
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

int Hunk::getAlignmentOffset() const {
	return m_alignmentOffset;
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

void Hunk::setRawSize(int size) {
	m_data.resize(size);
}

void Hunk::setAlignmentBits(int alignmentBits) {
	m_alignmentBits = alignmentBits;
}

void Hunk::setAlignmentOffset(int alignmentOffset) {
	m_alignmentOffset = alignmentOffset;
}

void Hunk::trim() {
	int farestReloc = 0;
	for(vector<relocation>::const_iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		int relocSize = 4;
		farestReloc = max(it->offset+relocSize, farestReloc);
	}

	while(m_data.size() > farestReloc && m_data.back() == 0)
		m_data.pop_back();
}

void Hunk::appendZeroes(int num) {
	while(num--)
		m_data.push_back(0);
}

//chop of x bytes from the initialized data
void Hunk::chop(int size) {
	m_data.erase((m_data.end()-size), m_data.end());
}

static int getType(int level) {
	switch(level) {
		case 0:
			return RECORD_SECTION;
		case 1:
			return RECORD_OLD_SECTION;
		case 2:
			return RECORD_PUBLIC;
	}
	return 0;
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
	CompressionReportRecord* codeSection = new CompressionReportRecord("Code sections", RECORD_SECTION|RECORD_CODE, 0, 0);
	CompressionReportRecord* dataSection = new CompressionReportRecord("Data sections", RECORD_SECTION, splittingPoint, sizefill[splittingPoint]);
	CompressionReportRecord* uninitSection = new CompressionReportRecord("Uninitialized sections", RECORD_SECTION, getRawSize(), -1);
	root->children.push_back(codeSection);
	root->children.push_back(dataSection);
	root->children.push_back(uninitSection);

	for(vector<Symbol*>::iterator it = symbols.begin(); it != symbols.end(); it++) {
		Symbol* sym = *it;
		CompressionReportRecord* c = new CompressionReportRecord(sym->name.c_str(), 
			0, sym->value, (sym->value < getRawSize()) ? sizefill[sym->value] : -1);

		//copy misc string
		c->miscString = sym->miscString;

		//set flags
		if(sym->flags & SYMBOL_IS_SECTION) {
			c->type |= RECORD_OLD_SECTION;
		}
		if(!(sym->flags & SYMBOL_IS_LOCAL)) {
			c->type |= RECORD_PUBLIC;
		}

		//find appropriate section
		CompressionReportRecord* r;	//parent section
		if(sym->value < splittingPoint) {
			r = codeSection;
		} else if(sym->value < getRawSize()) {
			r = dataSection;
		} else {
			r = uninitSection;
		}

		//find where to place the record
		while(r->getLevel()+1 < c->getLevel()) {
			if(r->children.empty()) {	//add a dummy element if we skip a level
				int level = r->getLevel()+1;
				string name = c->pos == r->pos ? c->name : r->name;
				CompressionReportRecord* dummy = new CompressionReportRecord(name.c_str(), 
					getType(level)|RECORD_DUMMY, sym->value, (sym->value < getRawSize()) ? sizefill[sym->value] : -1);
				if(level == 1)
					dummy->miscString = ".dummy";
				r->children.push_back(dummy);
			}
			r = r->children.back();
		}


		//add public symbol to start of sections, if they don't have one already
		if(sym->value < getRawSize() && (c->type & RECORD_OLD_SECTION) && sym->value < (*(it+1))->value) {
			CompressionReportRecord* dummy = new CompressionReportRecord(c->name.c_str(), 
				RECORD_PUBLIC|RECORD_DUMMY, sym->value, sizefill[sym->value]);
			c->children.push_back(dummy);
		}

		//add public symbol to start of sections, if they don't have one already
		if(sym->value < getRawSize() && (c->type & RECORD_PUBLIC) && sym->value < (*(it+1))->value) {
			CompressionReportRecord* dummy = new CompressionReportRecord(c->name.c_str(), 
				RECORD_DUMMY, sym->value, sizefill[sym->value]);
			c->children.push_back(dummy);
		}


		r->children.push_back(c);
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
	for(vector<relocation>::iterator it = m_relocations.begin(); it != m_relocations.end(); it++) {
		Symbol* s = findSymbol(it->symbolname.c_str());
		if(s && s->secondaryName.size() > 0)
			s = findSymbol(s->secondaryName.c_str());
		if(s && s->flags & SYMBOL_IS_RELOCATEABLE && s->flags & SYMBOL_IS_SECTION)
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

			offsetmap[s->value] = s;
		}
	}
	return offsetmap;
}

//roundFloats
//default round float: __real@XXXXXXXX, *@3MA
//default round float: tf_
//default round double: __real@XXXXXXXXXXXXXXXX, *@3NA
//round *tf_XX* to XX bits.
void Hunk::roundFloats(int defaultBits) {
	map<int, Symbol*> offset_to_symbol = getOffsetToSymbolMap();
	for(map<string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		Symbol* s = it->second;
		if(!(s->flags & SYMBOL_IS_RELOCATEABLE))
			continue;

		int bits = defaultBits;
		string name = stripCrinklerSymbolPrefix(s->name.c_str());
		string undecoratedName = undecorateSymbolName(s->name.c_str());
		bool isDouble = false;
		int a;
		if(startsWith(undecoratedName.c_str(), "tf_")) {
			isDouble = endsWith(name.c_str(), "NA");
		} else if(sscanf_s(undecoratedName.c_str(), "tf%d_", &bits) && bits >= 0 && bits <= 64) {
			isDouble = endsWith(name.c_str(), "NA");
		} else if((sscanf_s(name.c_str(), "__real@%16X", &a) && name.size() == (2+4+1+16)) ||
			endsWith(name.c_str(), "@3NA")) {
			isDouble = true;
			bits = defaultBits;
		} else if((sscanf_s(name.c_str(), "__real@%8X", &a) && name.size() == (2+4+1+8)) ||
			endsWith(name.c_str(), "@3MA")) {
			bits = defaultBits;
		} else {
			continue;
		}

		if(!isDouble && bits>=32)
			continue;


		//REMARK: a float with most significant byte = 0 could be trimmed down
		//and thus not get truncated. Just ignore it. It only happens to numbers with norm < 10^-38
		int address = s->value;
		int type_size = isDouble ? sizeof(double) : sizeof(float);
		while(address >= 0 && address <= getRawSize()-type_size) {
			for(int i = 1; i < type_size; i++) {
				if(offset_to_symbol.find(address+i) != offset_to_symbol.end())
					goto endit;
			}

			int* ptr = (int*)&m_data[address];
			if(isDouble) {
				double orgf = *(double*)ptr;
				int orgi0 = ptr[0];
				int orgi1 = ptr[1];
				*(unsigned long long*)ptr = roundInt64(*(unsigned long long*)ptr, bits);
				printf("%s[%d]: %f (0x%.8X%.8X 64 bits) -> %f (0x%.8X%.8X %d bits)\n",
					name.c_str(), address - s->value, orgf, orgi1, orgi0, *(double*)ptr, ptr[1], ptr[0], bits);
			} else {
				float orgf = *(float*)ptr;
				int orgi = ptr[0];
				ptr[0] = roundInt64(ptr[0], 32+bits);
				printf("%s[%d]: %f (0x%.8X 32 bits) -> %f (0x%.8X %d bits)\n",
					name.c_str(), address - s->value, orgf, orgi, *(float*)ptr, ptr[0], bits);
			}
			address += type_size;
			if(offset_to_symbol.find(address) != offset_to_symbol.end())
				break;
		}
endit:
		;
	}
}

void Hunk::overrideAlignment(int defaultBits) {
	bool align_label = false;
	for(map<string, Symbol*>::const_iterator jt = m_symbols.begin(); jt != m_symbols.end(); jt++) {
		Symbol* s = jt->second;
		string sname = s->getUndecoratedName();
		size_t apos = sname.find("align");
		if (apos != string::npos) {
			int align_bits = 0;
			int align_offset = 0;
			int n = sscanf_s(&sname.c_str()[apos+5], "%d_%d", &align_bits, &align_offset);
			if (n >= 1) {
				// align label match
				if (align_bits < 0 || align_bits > 30) {
					Log::error("", "Alignment label '%s' outside legal range for number of bits (0-30)", sname.c_str());
				}
				if (align_label) {
					Log::error("", "More than one alignment label in section '%s'", getName());
				}
				int offset = align_offset - s->value;
				setAlignmentBits(align_bits);
				setAlignmentOffset(offset);
				printf("Alignment of section '%s' overridden to %d bits", getName(), align_bits);
				if (offset > 0) printf(" + %d", offset);
				if (offset < 0) printf(" - %d", -offset);
				printf("\n");
				align_label = true;
			}
		}
	}

	if (!align_label && defaultBits != -1 && defaultBits > getAlignmentBits() && !(m_flags & HUNK_IS_CODE) && getRawSize() == 0) {
		// override uninitialized section alignment
		setAlignmentBits(defaultBits);
		printf("Alignment of section '%s' overridden to default %d bits\n", getName(), defaultBits);
	}

}

void Hunk::markHunkAsLibrary() {
	for(map<string, Symbol*>::iterator it = m_symbols.begin(); it != m_symbols.end(); it++) {
		it->second->fromLibrary = true;
	}
}