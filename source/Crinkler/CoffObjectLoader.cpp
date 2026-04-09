#include "CoffObjectLoader.h"

#include <windows.h>
#include <algorithm>
#include <cstring>
#include "Hunk.h"
#include "PartList.h"
#include "Symbol.h"
#include "StringMisc.h"

using namespace std;

// CodeView debug subsection types
static const unsigned int DEBUG_S_LINES      = 0xF2;
static const unsigned int DEBUG_S_STRINGTABLE = 0xF3;
static const unsigned int DEBUG_S_FILECHKSMS = 0xF4;

// CV_SIGNATURE for debug$S
static const unsigned int CV_SIGNATURE_C13   = 4;

struct CV_DebugSLinesHeader {
	unsigned int offCon;      // Offset of the contributed code in the section
	unsigned short segCon;    // Section index
	unsigned short flags;
	unsigned int cbCon;       // Size of the contributed code
};

struct CV_Line {
	unsigned int offset;      // Offset from start of contributed code
	unsigned int flags;       // Line number in low 24 bits, delta/statement in high bits
};

struct CV_Column {
	unsigned short offColumnStart;
	unsigned short offColumnEnd;
};

// File block header within DEBUG_S_LINES
struct CV_LinesFileBlockHeader {
	unsigned int offFile;     // Offset into file checksums subsection
	unsigned int nLines;
	unsigned int cbBlock;     // Size of this block (including this header)
};

struct CV_FileChecksum {
	unsigned int offFilename;   // Offset into string table
	unsigned char cbChecksum;
	unsigned char checksumType;
	// Followed by cbChecksum bytes of checksum data, then padding to align to 4
};

struct DebugSectionLineInfo {
	int sectionIndex;         // Which COFF section this applies to
	int offsetInSection;      // Offset within the section
	int fileChecksumOffset;   // Offset into file checksums table
	int line;
};

// Parse .debug$S section to extract file/line info and store it on the target hunks
static void ParseDebugS(const char* debugData, int debugSize,
	const IMAGE_SECTION_HEADER* sectionHeaders, int numSections,
	std::vector<Hunk*>& hunkList) {

	if (debugSize < 4) return;
	unsigned int signature = *(unsigned int*)debugData;
	if (signature != CV_SIGNATURE_C13) return;

	const char* stringTable = nullptr;
	int stringTableSize = 0;
	const char* fileChecksums = nullptr;
	int fileChecksumsSize = 0;

	// Collected line info to process after we have file tables
	struct RawLineEntry {
		int sectionIndex;
		int offset;
		int fileChecksumOffset;
		int line;
	};
	std::vector<RawLineEntry> rawLines;

	// First pass: find string table, file checksums, and line info
	const char* p = debugData + 4;
	const char* end = debugData + debugSize;
	while (p + 8 <= end) {
		unsigned int subsectionType = *(unsigned int*)p;
		unsigned int subsectionSize = *(unsigned int*)(p + 4);
		const char* subsectionData = p + 8;
		const char* subsectionEnd = subsectionData + subsectionSize;
		if (subsectionEnd > end) break;

		if (subsectionType == DEBUG_S_STRINGTABLE) {
			stringTable = subsectionData;
			stringTableSize = subsectionSize;
		} else if (subsectionType == DEBUG_S_FILECHKSMS) {
			fileChecksums = subsectionData;
			fileChecksumsSize = subsectionSize;
		} else if (subsectionType == DEBUG_S_LINES) {
			if (subsectionSize >= sizeof(CV_DebugSLinesHeader)) {
				const CV_DebugSLinesHeader* header = (const CV_DebugSLinesHeader*)subsectionData;
				bool hasColumns = (header->flags & 1) != 0;
				int sectionIndex = header->segCon - 1; // Convert 1-based to 0-based

				const char* blockPtr = subsectionData + sizeof(CV_DebugSLinesHeader);
				while (blockPtr + sizeof(CV_LinesFileBlockHeader) <= subsectionEnd) {
					const CV_LinesFileBlockHeader* fileBlock = (const CV_LinesFileBlockHeader*)blockPtr;
					const CV_Line* lines = (const CV_Line*)(blockPtr + sizeof(CV_LinesFileBlockHeader));

					for (unsigned int i = 0; i < fileBlock->nLines; i++) {
						if ((const char*)&lines[i + 1] > subsectionEnd) break;
						int lineNum = lines[i].flags & 0x00FFFFFF;
						RawLineEntry entry;
						entry.sectionIndex = sectionIndex;
						entry.offset = header->offCon + lines[i].offset;
						entry.fileChecksumOffset = fileBlock->offFile;
						entry.line = lineNum;
						rawLines.push_back(entry);
					}

					blockPtr += fileBlock->cbBlock;
				}
			}
		}

		// Advance to next subsection (aligned to 4 bytes)
		p = subsectionData + ((subsectionSize + 3) & ~3);
	}

	if (!stringTable || !fileChecksums || rawLines.empty()) return;

	// Build file checksum offset -> filename mapping
	// Also build per-hunk file index mapping
	struct HunkFileMapping {
		std::map<int, int> checksumOffsetToLocalIndex; // fileChecksumOffset -> index in hunk's m_debugFiles
	};
	std::map<int, HunkFileMapping> hunkFileMappings; // sectionIndex -> mapping

	for (const RawLineEntry& entry : rawLines) {
		if (entry.sectionIndex < 0 || entry.sectionIndex >= (int)hunkList.size()) continue;
		Hunk* hunk = hunkList[entry.sectionIndex];
		if (!hunk) continue;

		HunkFileMapping& mapping = hunkFileMappings[entry.sectionIndex];
		if (mapping.checksumOffsetToLocalIndex.find(entry.fileChecksumOffset) == mapping.checksumOffsetToLocalIndex.end()) {
			// Resolve filename from checksum offset
			if (entry.fileChecksumOffset + (int)sizeof(CV_FileChecksum) <= fileChecksumsSize) {
				const CV_FileChecksum* checksum = (const CV_FileChecksum*)(fileChecksums + entry.fileChecksumOffset);
				if ((int)checksum->offFilename < stringTableSize) {
					const char* filename = stringTable + checksum->offFilename;
					int localIndex = (int)hunk->GetDebugFiles().size();
					hunk->AddDebugFile(filename);
					mapping.checksumOffsetToLocalIndex[entry.fileChecksumOffset] = localIndex;
				}
			}
		}
	}

	// Now add line entries to hunks
	for (const RawLineEntry& entry : rawLines) {
		if (entry.sectionIndex < 0 || entry.sectionIndex >= (int)hunkList.size()) continue;
		Hunk* hunk = hunkList[entry.sectionIndex];
		if (!hunk) continue;

		auto& mapping = hunkFileMappings[entry.sectionIndex];
		auto it = mapping.checksumOffsetToLocalIndex.find(entry.fileChecksumOffset);
		if (it != mapping.checksumOffsetToLocalIndex.end()) {
			hunk->AddDebugLine(entry.offset, it->second, entry.line);
		}
	}

	// Sort debug lines by offset for each hunk
	for (Hunk* hunk : hunkList) {
		if (hunk && !hunk->GetDebugLines().empty()) {
			auto& lines = const_cast<std::vector<DebugLineEntry>&>(hunk->GetDebugLines());
			std::sort(lines.begin(), lines.end(), [](const DebugLineEntry& a, const DebugLineEntry& b) {
				return a.offset < b.offset;
			});
		}
	}
}

static int GetAlignmentBitsFromCharacteristics(int chars) {
	return max(((chars & 0x00F00000)>>20) - 1, 0);
}

static string GetSectionName(const IMAGE_SECTION_HEADER* section, const char* stringTable) {
	char tmp[9]; tmp[8] = 0;
	memcpy(tmp, section->Name, 8*sizeof(char));

	if(section->Name[0] == '/') {
		int offset = atoi(&tmp[1]);
		return string(&stringTable[offset]);
	} else {
		return tmp;
	}

}

static string GetSymbolName(const IMAGE_SYMBOL* symbol, const char* stringTable) {
	if(symbol->N.Name.Short == 0) {	// Long name
		return &stringTable[symbol->N.Name.Long];
	} else {	// Short name
		char tmp[9]; tmp[8] = 0;
		memcpy(tmp, symbol->N.ShortName, 8);
		return tmp;
	}
}

static string StripNumeral(const string& s) {
	int idx = (int)s.size()-1;
	while(idx >= 0 && s[idx] != '|') idx--;
	if (idx == 0) return s;
	return s.substr(0, idx);
}

CoffObjectLoader::~CoffObjectLoader() {
}

bool CoffObjectLoader::Clicks(const char* data, int size) const {
	//TODO: Implement a safer check
	return *(unsigned short*)data == IMAGE_FILE_MACHINE_I386;
}

bool CoffObjectLoader::Load(Part& part, const char* data, int size, const char* module, bool inLibrary) {
	int startHunkCount = part.GetNumHunks();
	const char* ptr = data;

	// Header
	const IMAGE_FILE_HEADER* header = (const IMAGE_FILE_HEADER*)ptr;
	ptr += sizeof(IMAGE_FILE_HEADER);

	// Symbol table pointer
	const IMAGE_SYMBOL* symbolTable = (const IMAGE_SYMBOL*)(data + header->PointerToSymbolTable);
	const char* stringTable = (const char*)symbolTable + header->NumberOfSymbols*sizeof(IMAGE_SYMBOL);

	// Section headers
	const IMAGE_SECTION_HEADER* sectionHeaders = (const IMAGE_SECTION_HEADER*)ptr;

	Hunk* constantsHunk;
	{
		char hunkName[1000];
		sprintf_s(hunkName, 1000, "c[%s]!constants", module);
		constantsHunk = new Hunk(hunkName, 0, 0, 1, 0, 0);
		part.AddHunkBack(constantsHunk);
	}

	
	// Load sections
	std::vector<Hunk*> LinearHunkList;
	LinearHunkList.resize(header->NumberOfSections);
	for(int i = 0; i < header->NumberOfSections; i++) {
		string sectionName = GetSectionName(&sectionHeaders[i], stringTable);
		int chars = sectionHeaders[i].Characteristics;
		char hunkName[1000];
		sprintf_s(hunkName, 1000, "h[%s](%d)!%s", module, i, sectionName.c_str());
		unsigned int flags = 0;
		if(chars & IMAGE_SCN_CNT_CODE)
			flags |= HUNK_IS_CODE;
		if(chars & IMAGE_SCN_MEM_WRITE)
			flags |= HUNK_IS_WRITEABLE;
		bool isInitialized = (chars & IMAGE_SCN_CNT_INITIALIZED_DATA || 
								chars & IMAGE_SCN_CNT_CODE);
		Hunk* hunk = new Hunk(hunkName, data+sectionHeaders[i].PointerToRawData,	// Data pointer
								flags, GetAlignmentBitsFromCharacteristics(chars),	// Alignment
								isInitialized ? sectionHeaders[i].SizeOfRawData : 0,
								sectionHeaders[i].SizeOfRawData);	// Virtual size
		LinearHunkList[i] = hunk;

		part.AddHunkBack(hunk);
		
		// Relocations
		const IMAGE_RELOCATION* relocs = (const IMAGE_RELOCATION*) (data + sectionHeaders[i].PointerToRelocations);
		int nRelocs = sectionHeaders[i].PointerToRelocations ? sectionHeaders[i].NumberOfRelocations : 0;
		for(int j = 0; j < nRelocs; j++) {
			Relocation r;
			int symbolIndex = relocs[j].SymbolTableIndex;
			const IMAGE_SYMBOL* symbol = &symbolTable[symbolIndex];
			string symbolName = GetSymbolName(symbol, stringTable);
			if(symbol->StorageClass == IMAGE_SYM_CLASS_STATIC || 
				symbol->StorageClass == IMAGE_SYM_CLASS_LABEL) {	// Local symbol reference
				// Construct local name
				char name[1000];
				sprintf_s(name, 1000, "l[%s(%d)]!%s", module, symbolIndex, symbolName.c_str());
				r.symbolname = name;
			} else {
				r.symbolname = symbolName;
			}
			r.offset = relocs[j].VirtualAddress;
			switch(relocs[j].Type) {
				case IMAGE_REL_I386_DIR32NB:
				case IMAGE_REL_I386_DIR32:
					r.type = RELOCTYPE_ABS32;
					break;
				case IMAGE_REL_I386_REL32:
					r.type = RELOCTYPE_REL32;
			}
			r.objectname = StripNumeral(StripPath(module));
			
			hunk->AddRelocation(r);
		}
	}

	// Parse .debug$S sections for source file/line info
	for (int i = 0; i < header->NumberOfSections; i++) {
		string sectionName = GetSectionName(&sectionHeaders[i], stringTable);
		if (sectionName == ".debug$S" && sectionHeaders[i].PointerToRawData != 0) {
			int debugSize = sectionHeaders[i].SizeOfRawData;
			const char* debugData = data + sectionHeaders[i].PointerToRawData;
			ParseDebugS(debugData, debugSize, sectionHeaders, header->NumberOfSections, LinearHunkList);
		}
	}

	// Symbols
	for(int i = 0; i < (int)header->NumberOfSymbols; i++) {
		const IMAGE_SYMBOL* sym = &symbolTable[i];
	
		// Skip unknown symbol types
		if(sym->StorageClass != IMAGE_SYM_CLASS_EXTERNAL &&
			sym->StorageClass != IMAGE_SYM_CLASS_STATIC &&
			sym->StorageClass != IMAGE_SYM_CLASS_LABEL &&
			sym->StorageClass != IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
				i += sym->NumberOfAuxSymbols;
				continue;
		}

		Symbol* s = new Symbol(GetSymbolName(sym, stringTable).c_str(), sym->Value, SYMBOL_IS_RELOCATEABLE, 0);

		if(sym->SectionNumber > 0) {
			s->hunk = LinearHunkList[sym->SectionNumber-1];

			if(sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->Type == 0x20 && sym->NumberOfAuxSymbols > 0) {	// Function definition
				const IMAGE_AUX_SYMBOL* aux = (const IMAGE_AUX_SYMBOL*) (sym+1);
				s->flags |= SYMBOL_IS_FUNCTION;
				s->size = aux->Sym.Misc.TotalSize;
			}

			if(sym->StorageClass == IMAGE_SYM_CLASS_STATIC ||	// Perform name mangling on local symbols
				sym->StorageClass == IMAGE_SYM_CLASS_LABEL) {

				char symname[1000];
				sprintf_s(symname, 1000, "l[%s(%d)]!%s", module, i, s->name.c_str());
				s->name = symname;
				s->flags |= SYMBOL_IS_LOCAL;
				if(sym->StorageClass == IMAGE_SYM_CLASS_STATIC && sym->NumberOfAuxSymbols == 1) {
					s->flags |= SYMBOL_IS_SECTION;
					s->miscString = module;
				}
			}
			s->hunk->AddSymbol(s);
		} else if(sym->SectionNumber == 0 && sym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL && sym->Value > 0) {
			// Create an uninitialised hunk
			char hunkName[1000];
			sprintf_s(hunkName, 1000, "u[%s]!%s", module, s->name.c_str());
			Hunk* uninitHunk = new Hunk(hunkName, NULL, HUNK_IS_WRITEABLE, 1, 0, s->value);
			s->hunk = uninitHunk;
			s->value = 0;
			uninitHunk->AddSymbol(s);
			part.AddHunkBack(uninitHunk);
		} else if(sym->SectionNumber == 0 && sym->StorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL && sym->Value == 0) {
			// Weak external
			const IMAGE_AUX_SYMBOL* aux = (const IMAGE_AUX_SYMBOL*) (sym+1);
			s->secondaryName = GetSymbolName(&symbolTable[aux->Sym.TagIndex], stringTable);
			s->hunk = constantsHunk;
			s->flags = 0;
			s->hunk->AddSymbol(s);
		} else if(sym->SectionNumber == -1) {	// Constant symbol
			s->hunk = constantsHunk;
			s->flags = 0;
			s->hunk->AddSymbol(s);
		} else {
			// Ignore unknown symbol type
			delete s;
		}
		
		i += sym->NumberOfAuxSymbols;	// Skip aux symbols
	}

	for (int i = startHunkCount; i < part.GetNumHunks(); i++) {
		if (inLibrary) {
			part[i]->MarkHunkAsLibrary();
		}
		part[i]->Trim();
	}
	
	return true;
}