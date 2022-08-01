#pragma once
#ifndef _HUNK_H_
#define _HUNK_H_

#include <string>
#include <map>
#include <vector>

const int HUNK_IS_CODE =		0x01;
const int HUNK_IS_WRITEABLE =	0x02;
const int HUNK_IS_IMPORT =		0x04;
const int HUNK_IS_LEADING =		0x08;
const int HUNK_IS_TRAILING =	0x10;
const int HUNK_IS_ALIGNED =		0x20;

class Symbol;
class Hunk;
class PartList;
enum RelocationType {RELOCTYPE_ABS32, RELOCTYPE_REL32};

const int RECORD_ROOT =			0x01;
const int RECORD_SECTION =		0x02;
const int RECORD_PUBLIC	=		0x04;
const int RECORD_OLD_SECTION =	0x10;
const int RECORD_CODE =			0x20;
const int RECORD_DUMMY =		0x40;		// Record is a dummy record not in the actual file

struct CompressionReportRecord {
	std::string name;
	int pos;
	int compressedPos;
	int type;
	std::vector<CompressionReportRecord*> children;
	int size;
	int compressedSize;
	std::string miscString; // For holding extra textual information about the symbol e.g. a section name.

	CompressionReportRecord(const char* name, int type, int pos, int compressedPos) {
		this->name = name;
		this->type = type;
		this->pos = pos;
		this->compressedPos = compressedPos;
	}

	~CompressionReportRecord() {
		for(CompressionReportRecord* record : children)
			delete record;
	}

	void CalculateSize(int size, int compressedSize) {
		this->size = size;
		this->compressedSize = compressedSize;
		int nchildren = (int)children.size()-1;
		for(int i = 0; i < nchildren; i++) {
			int nextCompressedSize = (children[i+1]->compressedPos >= 0) ? children[i+1]->compressedPos : (compressedPos + compressedSize);
			children[i]->CalculateSize(children[i+1]->pos - children[i]->pos, nextCompressedSize - children[i]->compressedPos);
		}
		if(!children.empty()) {
			CompressionReportRecord* child = children.back();
			child->CalculateSize(pos + size - child->pos, compressedPos + compressedSize - child->compressedPos);
		}
	}

	// Returns the level of the record. Level 0: section. Level 1: old section. Level 2: public symbol. Level 3: private symbol.
	int GetLevel() {
		if(type & RECORD_SECTION)
			return 0;
		if(type & RECORD_OLD_SECTION)
			return 1;
		if(type & RECORD_PUBLIC)
			return 2;
		return 3;
	}
};

struct Relocation {
	std::string		symbolname;
	int				offset;
	RelocationType	type;
	std::string		objectname;
};

class Hunk {
	friend class PartList;

	int				m_alignmentBits;
	int				m_alignmentOffset;
	unsigned int	m_flags;
	int				m_virtualsize;

	std::vector<char>	m_data;
	std::vector<Relocation> m_relocations;
	std::map<std::string, Symbol*> m_symbols;
	Symbol* m_continuation;
	std::string m_name;
	std::string m_importName;
	std::string m_importDll;
	std::string m_cached_id;

	int			m_numReferences;
public:
	Hunk(const Hunk& h);
	Hunk(const char* symbolName, const char* importName, const char* importDll);
	Hunk(const char* name, const char* data, unsigned int flags, int alignmentBits, int rawsize, int virtualsize);
	~Hunk();

	void		AddRelocation(Relocation r);
	int			GetNumRelocations() const						{ return (int)m_relocations.size(); }
	Relocation* GetRelocations()								{ return &m_relocations[0]; }
	void		AddSymbol(Symbol* s);
	void		SetContinuation(Symbol* s)						{ m_continuation = s; }
	Symbol*		GetContinuation() const							{ return m_continuation; }
	Symbol*		FindUndecoratedSymbol(const char* name) const;
	Symbol*		FindSymbol(const char* name) const;
	void		PrintSymbols() const;
	void		Relocate(int imageBase);
	void		SetVirtualSize(int size)						{ m_virtualsize = size; }
	void		SetRawSize(int size);
	void		SetAlignmentBits(int alignmentBits);
	void		SetAlignmentOffset(int alignmentOffset)			{ m_alignmentOffset = alignmentOffset; }
	void		Trim();
	void		RoundFloats(int defaultBits);
	void		OverrideAlignment(int defaultBits);
	void		AppendZeroes(int num);
	void		Insert(int offset, const unsigned char* data, int length);

	void		MarkHunkAsLibrary();

	CompressionReportRecord* GenerateCompressionSummary(PartList& parts, int* sizefill);


	int				GetAlignmentBits() const		{ return m_alignmentBits; }
	int				GetAlignmentOffset() const		{ return m_alignmentOffset; }
	unsigned int	GetFlags() const				{ return m_flags; }
	const char*		GetName() const					{ return m_name.c_str(); }
	char*			GetPtr()						{ return m_data.data(); }
	int				GetRawSize() const				{ return (int)m_data.size(); }
	int				GetVirtualSize() const			{ return m_virtualsize; }
	int				GetNumReferences() const		{ return m_numReferences; }
	const char*		GetImportName() const			{ return m_importName.c_str(); }
	const char*		GetImportDll() const			{ return m_importDll.c_str(); }
	void			SetImportDll(const char* dll)	{ m_importDll = dll; }
	std::map<int, Symbol*> GetOffsetToRelocationMap();
	std::map<int, Symbol*> GetOffsetToSymbolMap();
	const std::string& GetID();
};

#endif
