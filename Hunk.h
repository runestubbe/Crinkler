#pragma once
#ifndef _HUNK_H_
#define _HUNK_H_

#include <string>
#include <list>
#include <map>
#include <vector>

#define HUNK_IS_CODE		0x01
#define HUNK_IS_WRITEABLE	0x02
#define HUNK_IS_FIXED		0x04
#define HUNK_IS_IMPORT		0x08

class Symbol;
class Hunk;
class HunkList;
enum RelocationType {RELOCTYPE_ABS32, RELOCTYPE_REL32};

#define RECORD_ROOT 0x01
#define RECORD_SECTION 0x02
#define RECORD_PUBLIC 0x04
#define RECORD_FUNCTION 0x08

struct CompressionSummaryRecord {
	std::string name;
	int pos;
	int compressedPos;
	int type;
	std::vector<CompressionSummaryRecord*> children;
	int size;
	int compressedSize;
	int functionSize;
	int compressedFunctionSize;
	std::string functionName;

	CompressionSummaryRecord(const char* name, int type, int pos, int compressedPos) {
		this->name = name;
		this->type = type;
		this->pos = pos;
		this->compressedPos = compressedPos;
	}

	~CompressionSummaryRecord() {
		for(std::vector<CompressionSummaryRecord*>::iterator it = children.begin(); it != children.end(); it++)
			delete *it;
	}

	void calculateSize(int size, int compressedSize) {
		this->size = size;
		this->compressedSize = compressedSize;
		int nchildren = children.size()-1;
		for(int i = 0; i < nchildren; i++) {
			int nextCompressedSize = (children[i+1]->compressedPos >= 0) ? children[i+1]->compressedPos : (compressedPos + compressedSize);
			children[i]->calculateSize(children[i+1]->pos - children[i]->pos, nextCompressedSize - children[i]->compressedPos);
		}
		if(!children.empty()) {
			CompressionSummaryRecord* child = children.back();
			child->calculateSize(pos + size - child->pos, compressedPos + compressedSize - child->compressedPos);
		}
	}
};


struct relocation {
	std::string		symbolname;
	int				offset;
	RelocationType	type;
};

class Hunk {
	friend class HunkList;
	int				m_alignmentBits;
	unsigned int	m_flags;
	int				m_rawsize;
	int				m_virtualsize;

	char*	m_data;
	std::list<relocation> m_relocations;
	std::map<std::string, Symbol*> m_symbols;
	std::string m_name;
	std::string m_importName;
	std::string m_importDll;

	int			m_numReferences;
public:
	Hunk(const Hunk& h);
	Hunk(const char* symbolName, const char* importName, const char* importDll);
	Hunk(const char* name, const char* data, unsigned int flags, int alignmentBits, int rawsize, int virtualsize);
	~Hunk();

	void addRelocation(relocation r);
	void addSymbol(Symbol* s);
	Symbol* findUndecoratedSymbol(const char* name) const;
	Symbol* findSymbol(const char* name) const;
	void fixate();
	void printSymbols() const;
	void relocate(int imageBase);
	void setVirtualSize(int size);
	void setAlignmentBits(int alignmentBits);
	void trim();
	void chop(int size);

	CompressionSummaryRecord* getCompressionSummary(int* sizefill, int splittingPoint);


	int getAlignmentBits() const;
	unsigned int getFlags() const;
	const char* getName() const;
	char* getPtr();
	int getRawSize() const;
	int getVirtualSize() const;
	int getNumReferences() const;
	const char* getImportName() const;
	const char* getImportDll() const;
	void setImportDll(const char* dll);
};

#endif
