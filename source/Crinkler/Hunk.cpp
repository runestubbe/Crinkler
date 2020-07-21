#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>
#include "StringMisc.h"
#include "misc.h"

#include "Hunk.h"
#include "NameMangling.h"
#include "Log.h"
#include "Symbol.h"

using namespace std;

// Sort symbols by address, then by type (section < global < local) and finally by name
static bool SymbolComparator(Symbol* first, Symbol* second) {
	if(first->value != second->value) {
		return first->value < second->value;
	} else if(first->hunk_offset != second->hunk_offset) {
		return first->hunk_offset < second->hunk_offset;
	} else {
		if((first->flags & SYMBOL_IS_SECTION) != (second->flags & SYMBOL_IS_SECTION))
			return (first->flags & SYMBOL_IS_SECTION) != 0;
		if((first->flags & SYMBOL_IS_LOCAL) != (second->flags & SYMBOL_IS_LOCAL))
			return (first->flags & SYMBOL_IS_LOCAL) == 0;
		return first->name < second->name;
	}
}

static int GetType(int level) {
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

Hunk::Hunk(const Hunk& h) : 
	m_alignmentBits(h.m_alignmentBits), m_flags(h.m_flags), m_data(h.m_data),
	m_virtualsize(h.m_virtualsize), m_relocations(h.m_relocations), m_name(h.m_name),
	m_importName(h.m_importName), m_importDll(h.m_importDll), m_numReferences(0),
	m_alignmentOffset(0), m_continuation(NULL)
{
	// Deep copy symbols
	for(const auto& p : h.m_symbols) {
		Symbol* s = new Symbol(*p.second);
		s->hunk = this;
		m_symbols[p.second->name] = s;
	}
}


Hunk::Hunk(const char* symbolName, const char* importName, const char* importDll) :
	m_name(symbolName), m_virtualsize(0),
	m_flags(HUNK_IS_IMPORT), m_alignmentBits(0), m_importName(importName),
	m_importDll(importDll), m_numReferences(0),
	m_alignmentOffset(0), m_continuation(NULL)
{
	AddSymbol(new Symbol(symbolName, 0, SYMBOL_IS_RELOCATEABLE, this));
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
	// Free symbols
	for(auto& p : m_symbols) {
		delete p.second;
	}
}

void Hunk::AddSymbol(Symbol* s) {
	map<string, Symbol*>::iterator it = m_symbols.find(s->name.c_str());
	if(it == m_symbols.end()) {
		m_symbols.insert(make_pair(s->name, s));
	} else {
		Symbol* oldSym = it->second;
		if(oldSym->secondaryName.size() > 0) {
			// Overwrite weak symbols
			m_symbols[s->name.c_str()] = s;
			delete oldSym;
		} else {
			delete s;
		}
	}
}

void Hunk::AddRelocation(Relocation r) {
	assert(r.offset >= 0);
	assert(r.offset <= GetRawSize()-4);
	m_relocations.push_back(r);
}

void Hunk::PrintSymbols() const {
	
	// Extract relocatable symbols
	vector<Symbol*> symbols;
	for(const auto& p : m_symbols) {
		if(p.second->flags & SYMBOL_IS_RELOCATEABLE)
			symbols.push_back(p.second);
	}

	// Sort symbol by value
	sort(symbols.begin(), symbols.end(), SymbolComparator);

	for(Symbol* symbol : symbols) {
		printf("%8x: %s\n", symbol->value, symbol->name.c_str());
	}
}

Symbol* Hunk::FindUndecoratedSymbol(const char* name) const {
	for(const auto& p : m_symbols) {
		Symbol* s = p.second;
		if(UndecorateSymbolName(s->name.c_str()).compare(name) == 0)
			return s;
	}
	return NULL;
}

Symbol* Hunk::FindSymbol(const char* name) const {
	map<string, Symbol*>::const_iterator it = m_symbols.find(name);
	if(it != m_symbols.end())
		return it->second;
	else
		return NULL;
}

// Generate a help message based on the symbol name
static const char* HelpMessage(const char* name) {
	if(StartsWith(name, "__RTC_")) {
		return "Disable 'Basic Runtime Checks' in the compiler options.";
	} else if(StartsWith(name, "__security_cookie")) {
		return "Disable 'Buffer Security Check' in the compiler options.";
	} else if(StartsWith(name, "_crt_debugger_hook") || StartsWith(name, "___ImageBase")) {
		return "Define your own entry point.";
	} else if(StartsWith(name, "__ftol")) {
		return "Suppress _ftol calls with the /QIfist compiler option (warning: changes rounding mode of float-to-int conversions).";
	} else if(StartsWith(name, "__Cxx")) {
		return "Don't use exceptions.";
	} else if(StartsWith(name, "__imp_??1exception@std@")) {
		return "Don't use STL.";
	} else if(StartsWith(name, "__alloca") || StartsWith(name, "__chkstk")) {
		return "Avoid declaring large arrays or structs inside functions. Use global variables instead.";
	}

	return nullptr;
}

void Hunk::Relocate(int imageBase) {
	bool error = false;
	for(const Relocation& relocation : m_relocations) {
		// Find symbol
		Symbol* s = FindSymbol(relocation.symbolname.c_str());
		if(s && s->secondaryName.size() > 0)
			s = FindSymbol(s->secondaryName.c_str());
		if(s != NULL) {
			// Perform relocation
			int* word = (int*)&m_data[relocation.offset];
			switch(relocation.type) {
				case RELOCTYPE_ABS32:
					(*word) += s->value;
					if(s->flags & SYMBOL_IS_RELOCATEABLE)
						(*word) += imageBase;
					break;
				case RELOCTYPE_REL32:
					(*word) += s->value - relocation.offset - 4;
					break;
			}
		} else {
			string location = relocation.objectname;
			auto symbolMap = GetOffsetToSymbolMap();
			for (int offset = relocation.offset; offset >= 0; offset--) {
				if (symbolMap.count(offset)) {
					Symbol* symbol = symbolMap[offset];
					if (!(symbol->flags & SYMBOL_IS_LOCAL)) {
						location += ": ";
						location += symbol->GetUndecoratedName();
						break;
					}
				}
			}
			const char* helpMessage = HelpMessage(relocation.symbolname.c_str());
			if (helpMessage) {
				Log::NonfatalError(location.c_str(), "Cannot find symbol '%s'.\n * HINT: %s", relocation.symbolname.c_str(), helpMessage);
			} else {
				Log::NonfatalError(location.c_str(), "Cannot find symbol '%s'", relocation.symbolname.c_str());
			}
			error = true;
		}

	}

	if (error) {
		printf("\n\n");
		exit(-1);
	}
}

void Hunk::SetRawSize(int size) {
	m_data.resize(size);
}

void Hunk::SetAlignmentBits(int alignmentBits) {
	m_alignmentBits = alignmentBits;
	m_flags |= HUNK_IS_ALIGNED;
}

void Hunk::Trim() {
	int farestReloc = 0;
	for(const Relocation& relocation : m_relocations) {
		int relocSize = 4;
		farestReloc = max(relocation.offset+relocSize, farestReloc);
	}

	while((int)m_data.size() > farestReloc && m_data.back() == 0)
		m_data.pop_back();
}

void Hunk::AppendZeroes(int num) {
	while(num--)
		m_data.push_back(0);
}

void Hunk::Insert(int offset, const unsigned char* data, int size) {
	m_data.resize(m_data.size() + size);
	memmove(&m_data[offset + size], &m_data[offset], m_data.size() - (offset + size));
	memcpy(&m_data[offset], data, size);
	m_virtualsize += size;

	// Adjust symbols and relocations
	for (auto& p : m_symbols) {
		if (p.second->flags & SYMBOL_IS_RELOCATEABLE && p.second->value >= offset) p.second->value += size;
	}
	for (Relocation& relocation : m_relocations) {
		if (relocation.offset >= offset) relocation.offset += size;
	}
}


CompressionReportRecord* Hunk::GetCompressionSummary(int* sizefill, int splittingPoint) {
	vector<Symbol*> symbols;
	// Extract relocatable symbols
	for(const auto& p : m_symbols) {
		if(p.second->flags & SYMBOL_IS_RELOCATEABLE)
			symbols.push_back(p.second);
	}

	// Sort symbols
	sort(symbols.begin(), symbols.end(), SymbolComparator);

	CompressionReportRecord* root = new CompressionReportRecord("root", RECORD_ROOT, 0, 0);
	CompressionReportRecord* codeSection = new CompressionReportRecord("Code sections", RECORD_SECTION|RECORD_CODE, 0, 0);
	CompressionReportRecord* dataSection = new CompressionReportRecord("Data sections", RECORD_SECTION, splittingPoint, sizefill[splittingPoint]);
	CompressionReportRecord* uninitSection = new CompressionReportRecord("Uninitialized sections", RECORD_SECTION, GetRawSize(), -1);
	root->children.push_back(codeSection);
	root->children.push_back(dataSection);
	root->children.push_back(uninitSection);

	for(vector<Symbol*>::iterator it = symbols.begin(); it != symbols.end(); it++) {
		Symbol* sym = *it;
		CompressionReportRecord* c = new CompressionReportRecord(sym->name.c_str(), 
			0, sym->value, (sym->value < GetRawSize()) ? sizefill[sym->value] : -1);

		// Copy misc string
		c->miscString = sym->miscString;

		// Set flags
		if(sym->flags & SYMBOL_IS_SECTION) {
			c->type |= RECORD_OLD_SECTION;
		}
		if(!(sym->flags & SYMBOL_IS_LOCAL)) {
			c->type |= RECORD_PUBLIC;
		}

		// Find appropriate section
		CompressionReportRecord* r;	// Parent section
		if(sym->value < splittingPoint) {
			r = codeSection;
		} else if(sym->value < GetRawSize()) {
			r = dataSection;
		} else {
			r = uninitSection;
		}

		// Find where to place the record
		while(r->GetLevel()+1 < c->GetLevel()) {
			if(r->children.empty()) {	// Add a dummy element if we skip a level
				int level = r->GetLevel()+1;
				string name = c->pos == r->pos ? c->name : r->name;
				CompressionReportRecord* dummy = new CompressionReportRecord(name.c_str(), 
					GetType(level)|RECORD_DUMMY, sym->value, (sym->value < GetRawSize()) ? sizefill[sym->value] : -1);
				if(level == 1)
					dummy->miscString = ".dummy";
				r->children.push_back(dummy);
			}
			r = r->children.back();
		}

		bool less_than_next = (it + 1) == symbols.end() || sym->value < (*(it + 1))->value;

		// Add public symbol to start of sections, if they don't have one already
		if(sym->value < GetRawSize() && (c->type & RECORD_OLD_SECTION) && less_than_next) {
			CompressionReportRecord* dummy = new CompressionReportRecord(c->name.c_str(), 
				RECORD_PUBLIC|RECORD_DUMMY, sym->value, sizefill[sym->value]);
			c->children.push_back(dummy);
		}

		// Add public symbol to start of sections, if they don't have one already
		if(sym->value < GetRawSize() && (c->type & RECORD_PUBLIC) && less_than_next) {
			CompressionReportRecord* dummy = new CompressionReportRecord(c->name.c_str(), 
				RECORD_DUMMY, sym->value, sizefill[sym->value]);
			c->children.push_back(dummy);
		}


		r->children.push_back(c);
	}

	root->CalculateSize(m_virtualsize, sizefill[GetRawSize()]);
	return root;
}

map<int, Symbol*> Hunk::GetOffsetToRelocationMap() {
	map<int, Symbol*> offsetmap;
	map<int, Symbol*> symbolmap = GetOffsetToSymbolMap();
	for(Relocation& relocation : m_relocations) {
		Symbol* s = FindSymbol(relocation.symbolname.c_str());
		if(s && s->secondaryName.size() > 0)
			s = FindSymbol(s->secondaryName.c_str());
		if(s && s->flags & SYMBOL_IS_RELOCATEABLE && s->flags & SYMBOL_IS_SECTION)
			s = symbolmap.find(s->value)->second;	// Replace relocation to section with non-section
		offsetmap.insert(make_pair(relocation.offset, s));
	}
	return offsetmap;
}

map<int, Symbol*> Hunk::GetOffsetToSymbolMap() {
	map<int, Symbol*> offsetmap;
	for(const auto& p : m_symbols) {
		Symbol* s = p.second;
		if(s->flags & SYMBOL_IS_RELOCATEABLE) {
			if(offsetmap.find(s->value) != offsetmap.end() && s->flags & SYMBOL_IS_SECTION)	// Favor non-sections
				continue;

			offsetmap[s->value] = s;
		}
	}
	return offsetmap;
}

// roundFloats
// Default round float: __real@XXXXXXXX, *@3MA
// Default round float: tf_
// Default round double: __real@XXXXXXXXXXXXXXXX, *@3NA
// Round *tf_XX* to XX bits.
void Hunk::RoundFloats(int defaultBits) {
	map<int, Symbol*> offset_to_symbol = GetOffsetToSymbolMap();
	for(const auto& p : m_symbols) {
		Symbol* s = p.second;
		if(!(s->flags & SYMBOL_IS_RELOCATEABLE))
			continue;

		int bits = defaultBits;
		string name = StripCrinklerSymbolPrefix(s->name.c_str());
		string undecoratedName = UndecorateSymbolName(s->name.c_str());
		bool isDouble = false;
		int a;
		if(StartsWith(undecoratedName.c_str(), "tf_")) {
			isDouble = EndsWith(name.c_str(), "NA");
		} else if(sscanf_s(undecoratedName.c_str(), "tf%d_", &bits) && bits >= 0 && bits <= 64) {
			isDouble = EndsWith(name.c_str(), "NA");
		} else if((sscanf_s(name.c_str(), "__real@%16X", &a) && name.size() == (2+4+1+16)) ||
			EndsWith(name.c_str(), "@3NA")) {
			isDouble = true;
			bits = defaultBits;
		} else if((sscanf_s(name.c_str(), "__real@%8X", &a) && name.size() == (2+4+1+8)) ||
			EndsWith(name.c_str(), "@3MA")) {
			bits = defaultBits;
		} else {
			continue;
		}

		if(!isDouble && bits>=32)
			continue;


		// REMARK: A float with most significant byte = 0 could be trimmed down
		// and thus not get truncated. Just ignore it. It only happens to numbers with norm < 10^-38
		int address = s->value;
		int type_size = isDouble ? sizeof(double) : sizeof(float);
		while(address >= 0 && address <= GetRawSize()-type_size) {
			for(int i = 1; i < type_size; i++) {
				if(offset_to_symbol.find(address+i) != offset_to_symbol.end())
					goto endit;
			}

			int* ptr = (int*)&m_data[address];
			if(isDouble) {
				double orgf = *(double*)ptr;
				int orgi0 = ptr[0];
				int orgi1 = ptr[1];
				*(unsigned long long*)ptr = RoundInt64(*(unsigned long long*)ptr, bits);
				printf("%s[%d]: %f (0x%.8X%.8X 64 bits) -> %f (0x%.8X%.8X %d bits)\n",
					name.c_str(), address - s->value, orgf, orgi1, orgi0, *(double*)ptr, ptr[1], ptr[0], bits);
			} else {
				float orgf = *(float*)ptr;
				int orgi = ptr[0];
				ptr[0] = (int)RoundInt64(ptr[0], 32+bits);
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

void Hunk::OverrideAlignment(int defaultBits) {
	bool align_label = false;
	for(const auto& p : m_symbols) {
		Symbol* s = p.second;
		string sname = s->GetUndecoratedName();
		size_t apos = sname.find("align");
		if (apos != string::npos) {
			int align_bits = 0;
			int align_offset = 0;
			int n = sscanf_s(&sname.c_str()[apos+5], "%d_%d", &align_bits, &align_offset);
			if (n >= 1) {
				// Align label match
				if (align_bits < 0 || align_bits > 30) {
					Log::Error("", "Alignment label '%s' outside legal range for number of bits (0-30)", sname.c_str());
				}
				if (align_label) {
					Log::Error("", "More than one alignment label in section '%s'", GetName());
				}
				int offset = align_offset - s->value;
				SetAlignmentBits(align_bits);
				SetAlignmentOffset(offset);
				printf("Alignment of section '%s' overridden to %d bits", GetName(), align_bits);
				if (offset > 0) printf(" + %d", offset);
				if (offset < 0) printf(" - %d", -offset);
				printf("\n");
				align_label = true;
			}
		}
	}

	if (!align_label && defaultBits != -1 && defaultBits > GetAlignmentBits() && !(m_flags & HUNK_IS_CODE) && GetRawSize() == 0) {
		// Override uninitialized section alignment
		SetAlignmentBits(defaultBits);
		printf("Alignment of section '%s' overridden to default %d bits\n", GetName(), defaultBits);
	}

}

void Hunk::MarkHunkAsLibrary() {
	for(auto& p : m_symbols) {
		p.second->fromLibrary = true;
	}
}

const string& Hunk::GetID() {
	if (m_cached_id.empty()) {
		Symbol *first = nullptr;
		for (auto s : m_symbols) {
			if (!(s.second->flags & SYMBOL_IS_SECTION)) {
				if (first == nullptr || SymbolComparator(s.second, first)) {
					first = s.second;
				}
			}
		}
		string section_name;
		int i0 = (int)m_name.find_first_of('[', 0);
		int i1 = (int)m_name.find_last_of('\\');
		int i2 = (int)m_name.find_first_of(']', 0);
		int i3 = (int)m_name.find_last_of('!');
		i1 = max(i0, i1);
		if (i1 != -1 && i2 != -1 && i3 != -1 && i1 < i2 && i2 < i3) {
			section_name = m_name.substr(i1 + 1, i2 - (i1 + 1)) + ":" + m_name.substr(i3 + 1);
		}
		else {
			section_name = m_name;
		}
		m_cached_id = section_name + ":" + StripCrinklerSymbolPrefix(first->name.c_str());
	}
	return m_cached_id;
}
