#include "KkpReport.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <algorithm>

#include "../Compressor/Compressor.h"
#include "Hunk.h"
#include "Log.h"
#include "NameMangling.h"
#include "StringMisc.h"

// KKP binary format structures (matching kkpView parser)
#pragma pack(push, 1)
struct KkpByteData {
	unsigned char data;
	short symbol;
	double packed;
	short line;
	short file;
};
#pragma pack(pop)

struct KkpSymbolInfo {
	std::string name;
	int pos;			// Position in uncompressed data
	int size;			// Uncompressed size
	double packedSize;	// Compressed size in bytes
	bool isCode;
	int fileID;			// Index into file table (-1 if none)
};

struct KkpFileInfo {
	std::string name;
	int unpackedSize;
	double packedSize;
};

// Write a null-terminated string to file
static void WriteASCIIZ(FILE* f, const std::string& s) {
	fwrite(s.c_str(), 1, s.size() + 1, f);
}

// Flatten the CompressionReportRecord tree into a list of leaf symbols
static void FlattenSymbols(CompressionReportRecord* record, const int* sizefill, int rawSize,
	std::vector<KkpSymbolInfo>& symbols) {

	// Recurse into children to find leaf-level records
	if (!record->children.empty()) {
		for (CompressionReportRecord* child : record->children) {
			FlattenSymbols(child, sizefill, rawSize, symbols);
		}
		return;
	}

	// Leaf record — this is a symbol we want to emit
	if (record->size <= 0) return;

	KkpSymbolInfo sym;
	sym.name = StripCrinklerSymbolPrefix(record->name.c_str());
	sym.pos = record->pos;
	sym.size = record->size;
	sym.isCode = (record->type & RECORD_CODE) != 0;
	sym.fileID = -1;

	// Compute packed size from sizefill
	int startPos = record->pos;
	int endPos = record->pos + record->size;
	if (startPos < rawSize && endPos <= rawSize && startPos < endPos) {
		sym.packedSize = (sizefill[endPos] - sizefill[startPos]) / (BIT_PRECISION * 8.0);
	} else {
		sym.packedSize = 0.0;
	}

	symbols.push_back(sym);
}

// Collect symbols at a useful granularity: sections contain public symbols, etc.
// We want the most detailed level that has names.
static void CollectSymbols(CompressionReportRecord* record, const int* sizefill, int rawSize,
	std::vector<KkpSymbolInfo>& symbols) {

	if (record->type & RECORD_ROOT) {
		for (CompressionReportRecord* child : record->children)
			CollectSymbols(child, sizefill, rawSize, symbols);
		return;
	}

	if (record->type & RECORD_PART) {
		if (record->children.empty()) {
			// Part with no children — emit as a symbol
			FlattenSymbols(record, sizefill, rawSize, symbols);
		} else {
			for (CompressionReportRecord* child : record->children)
				CollectSymbols(child, sizefill, rawSize, symbols);
		}
		return;
	}

	if (record->type & RECORD_SECTION) {
		if (record->children.empty()) {
			FlattenSymbols(record, sizefill, rawSize, symbols);
		} else {
			for (CompressionReportRecord* child : record->children)
				CollectSymbols(child, sizefill, rawSize, symbols);
		}
		return;
	}

	// Public or private symbol — emit
	FlattenSymbols(record, sizefill, rawSize, symbols);
}

void KkpReport(CompressionReportRecord* csr, const char* filename, Hunk& hunk, const int* sizefill) {
	int rawSize = hunk.GetRawSize();
	if (rawSize <= 0) return;

	// Collect symbols
	std::vector<KkpSymbolInfo> symbols;
	CollectSymbols(csr, sizefill, rawSize, symbols);

	// Sort symbols by position
	std::sort(symbols.begin(), symbols.end(), [](const KkpSymbolInfo& a, const KkpSymbolInfo& b) {
		return a.pos < b.pos;
	});

	// Build file table from debug info on the hunk
	std::vector<KkpFileInfo> files;
	std::map<std::string, int> fileNameToIndex;

	const auto& debugFiles = hunk.GetDebugFiles();
	for (const DebugFileEntry& df : debugFiles) {
		std::string name = df.filename;
		if (fileNameToIndex.find(name) == fileNameToIndex.end()) {
			int idx = (int)files.size();
			fileNameToIndex[name] = idx;
			files.push_back({name, 0, 0.0});
		}
	}

	// Build per-byte debug line lookup from hunk debug lines
	// debugLines are sorted by offset - find file/line for each byte using upper_bound
	const auto& debugLines = hunk.GetDebugLines();

	// Map debug file indices (local to merged hunk) to our file table indices
	auto getFileIndex = [&](int debugFileIdx) -> int {
		if (debugFileIdx < 0 || debugFileIdx >= (int)debugFiles.size()) return -1;
		auto it = fileNameToIndex.find(debugFiles[debugFileIdx].filename);
		return (it != fileNameToIndex.end()) ? it->second : -1;
	};

	// Build per-byte symbol index map
	// For each byte, find which symbol it belongs to
	std::vector<short> byteSymbolMap(rawSize, -1);
	for (int si = 0; si < (int)symbols.size(); si++) {
		int start = symbols[si].pos;
		int end = start + symbols[si].size;
		if (start < 0) start = 0;
		if (end > rawSize) end = rawSize;
		for (int b = start; b < end; b++) {
			byteSymbolMap[b] = (short)si;
		}
	}

	// Assign file IDs to symbols using debug line info
	for (int si = 0; si < (int)symbols.size(); si++) {
		int symPos = symbols[si].pos;
		// Find the debug line entry for this symbol's start position
		auto it = std::upper_bound(debugLines.begin(), debugLines.end(), symPos,
			[](int offset, const DebugLineEntry& e) { return offset < e.offset; });
		if (it != debugLines.begin()) {
			--it;
			int fi = getFileIndex(it->fileIndex);
			if (fi >= 0) {
				symbols[si].fileID = fi;
			}
		}
	}

	// Accumulate per-file sizes
	for (const KkpSymbolInfo& sym : symbols) {
		if (sym.fileID >= 0 && sym.fileID < (int)files.size()) {
			files[sym.fileID].unpackedSize += sym.size;
			files[sym.fileID].packedSize += sym.packedSize;
		}
	}

	// If no debug files were found, create a single placeholder file
	if (files.empty()) {
		files.push_back({"<unknown>", rawSize, 0.0});
		// Compute total packed size
		double totalPacked = (sizefill[rawSize] - sizefill[0]) / (BIT_PRECISION * 8.0);
		files[0].packedSize = totalPacked;
		files[0].unpackedSize = rawSize;
		for (auto& sym : symbols) {
			sym.fileID = 0;
		}
	}

	// Write the KKP file
	FILE* f;
	if (fopen_s(&f, filename, "wb") || !f) {
		Log::Error("", "Cannot open '%s' for writing", filename);
		return;
	}

	// Header
	fwrite("KK64", 1, 4, f);
	int sourceSize = rawSize;
	fwrite(&sourceSize, 4, 1, f);
	int fileCount = (int)files.size();
	fwrite(&fileCount, 4, 1, f);

	// File entries
	for (const KkpFileInfo& file : files) {
		WriteASCIIZ(f, file.name);
		int packedInt = (int)(file.packedSize + 0.5);
		fwrite(&packedInt, 4, 1, f);
		fwrite(&file.unpackedSize, 4, 1, f);
	}

	// Symbol count
	int symbolCount = (int)symbols.size();
	fwrite(&symbolCount, 4, 1, f);

	// Symbol entries
	for (const KkpSymbolInfo& sym : symbols) {
		WriteASCIIZ(f, sym.name);
		double packedSize = sym.packedSize;
		fwrite(&packedSize, 8, 1, f);
		fwrite(&sym.size, 4, 1, f);
		unsigned char isCode = sym.isCode ? 1 : 0;
		fwrite(&isCode, 1, 1, f);
		int fileID = sym.fileID >= 0 ? sym.fileID : 0;
		fwrite(&fileID, 4, 1, f);
		unsigned int srcPos = (unsigned int)sym.pos;
		fwrite(&srcPos, 4, 1, f);
	}

	// Byte data
	const unsigned char* data = hunk.GetPtr();
	for (int i = 0; i < rawSize; i++) {
		KkpByteData bd;
		bd.data = data[i];
		bd.symbol = byteSymbolMap[i];
		bd.packed = (sizefill[i + 1] - sizefill[i]) / (BIT_PRECISION * 8.0);
		bd.line = 0;
		bd.file = -1;

		// Look up debug line info for this byte
		auto it = std::upper_bound(debugLines.begin(), debugLines.end(), i,
			[](int offset, const DebugLineEntry& e) { return offset < e.offset; });
		if (it != debugLines.begin()) {
			--it;
			int fi = getFileIndex(it->fileIndex);
			if (fi >= 0) {
				bd.file = (short)fi;
				bd.line = (short)it->line;
			}
		}

		fwrite(&bd, sizeof(KkpByteData), 1, f);
	}

	fclose(f);
}
