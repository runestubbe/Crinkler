#pragma once
#ifndef _CRINKLER_H_
#define _CRINKLER_H_

#include <map>
#include <set>
#include <string>
#include <cstdio>

#include "MultiLoader.h"
#include "PartList.h"
#include "../Compressor/Compressor.h"
#include "Transform.h"
#include "StringMisc.h"
#include "ConsoleProgressBar.h"
#include "WindowProgressBar.h"
#include "CompositeProgressBar.h"
#include "Export.h"
#include "Reuse.h"


class HunkLoader;

static const int CRINKLER_IMAGEBASE =	0x400000;
static const int CRINKLER_SECTIONSIZE = 0x10000;
static const int CRINKLER_SECTIONBASE = CRINKLER_IMAGEBASE+CRINKLER_SECTIONSIZE;
static const int CRINKLER_CODEBASE =	CRINKLER_IMAGEBASE+2*CRINKLER_SECTIONSIZE;
static const int CRINKLER_BASEPROB =	DEFAULT_BASEPROB;

enum SubsystemType {SUBSYSTEM_CONSOLE, SUBSYSTEM_WINDOWS};

static const int PRINT_LABELS =		1;
static const int PRINT_IMPORTS =	2;
static const int PRINT_MODELS =		4;

#define CRINKLER_TITLE "Crinkler 2.3 (" __DATE__ ") (c) 2005-2020 Aske Simon Christensen & Rune Stubbe"
#define CRINKLER_WITH_VERSION "Crinkler 2.3"
static const int CRINKLER_LINKER_VERSION = 0x3332;

class Crinkler {
	MultiLoader							m_hunkLoader;
	PartList							m_parts;
	std::string							m_entry;
	std::string							m_summaryFilename;
	std::string							m_reuseFilename;
	SubsystemType						m_subsystem;
	int									m_hashsize;
	int									m_hashtries;
	int									m_hunktries;
	int									m_printFlags;
	bool								m_useSafeImporting;
	CompressionType						m_compressionType;
	ReuseType							m_reuseType;
	std::vector<std::string>			m_rangeDlls;
	std::map<std::string, std::string>	m_replaceDlls;
	std::map<std::string, std::string>	m_fallbackDlls;
	std::set<Export>					m_exports;
	bool								m_stripExports;
	bool								m_showProgressBar;
	Transform*							m_transform;
	bool								m_useTinyHeader;
	bool								m_useTinyImport;
	bool								m_truncateFloats;
	int									m_truncateBits;
	bool								m_overrideAlignments;
	bool								m_unalignCode;
	int									m_alignmentBits;
	bool								m_runInitializers;
	int									m_largeAddressAware;
	int									m_saturate;

	ConsoleProgressBar					m_consoleBar;
	WindowProgressBar					m_windowBar;
	CompositeProgressBar				m_progressBar;

	Symbol*	FindEntryPoint();
	void RemoveUnreferencedHunks(Hunk* base);
	std::string GetEntrySymbolName() const;
	void ReplaceDlls(PartList& parts);
	void OverrideAlignments(PartList& parts);

	void LoadImportCode(bool use1kMode, bool useSafeImporting, bool useDllFallback, bool useRangeImport);
	Hunk* CreateModelHunk4k(PartList& parts);
	Hunk* CreateDynamicInitializerHunk();
	void InitProgressBar();
	void DeinitProgressBar();

	Hunk *FinalLink(PartList& parts, Hunk *header, Hunk *depacker, Hunk *hashHunk, Hunk *phase1, unsigned char *data, int size, int hashsize);

	int OptimizeHashsize(PartList& parts, unsigned char* data, int datasize, int hashsize, int tries);
	int EstimateModels(PartList& parts, unsigned char* data, int datasize, bool reestimate, bool use1kMode);
	void SetHeaderSaturation(Hunk* header);
	void SetHeaderConstants1k(Hunk* header, Hunk* phase1, int boostfactor, int baseprob0, int baseprob1, unsigned int modelmask, int subsystemVersion);
	void SetHeaderConstants4k(Hunk* header, Hunk* phase1, PartList& Parts, int hashsize, int subsystemVersion, int exportsRVA);
	void SetHeaderConstantsCommon(Hunk* header, int subsystemVersion);

public:
	Crinkler();
	~Crinkler();

	void Load(const char* filename);
	void Load(const char* data, int size, const char* module);
	void AddRuntimeLibrary();
	void Recompress(const char* input_filename, const char* output_filename);
	
	void Link(const char* filename);

	void SetUnalignCode(bool unalign)						{ m_unalignCode = unalign; }
	void AddRangeDll(const char* dllname)					{ m_rangeDlls.push_back(dllname); }
	void AddReplaceDll(const char* dll1, const char* dll2)	{ m_replaceDlls.insert(make_pair(ToLower(dll1), ToLower(dll2))); }
	void AddFallbackDll(const char* dll1, const char* dll2)	{ m_fallbackDlls.insert(make_pair(ToLower(dll1), ToLower(dll2))); }
	void ClearRangeDlls()									{ m_rangeDlls.clear(); }
	void AddExport(Export e)								{ if (m_exports.count(e) == 0) m_exports.insert(std::move(e)); }
	const std::set<Export>& GetExports()					{ return m_exports; }
	void SetStripExports(bool strip)						{ m_stripExports = strip; }
	void ShowProgressBar(bool show)							{ m_showProgressBar = show; }

	void SetUseTinyHeader(bool useTinyHeader)				{ m_useTinyHeader = useTinyHeader; }
	void SetUseTinyImport(bool useTinyImport)				{ m_useTinyImport = useTinyImport; }

	void SetEntry(const char* entry)						{ m_entry = entry; }
	void SetSubsystem(SubsystemType subsystem)				{ m_subsystem = subsystem; }

	void SetCompressionType(CompressionType compressionType){ m_compressionType = compressionType; }
	void SetHashsize(int hashsize)							{ m_hashsize = hashsize*1024*1024; }
	void SetHashtries(int hashtries)						{ m_hashtries = hashtries; }
	void SetHunktries(int hunktries)						{ m_hunktries = hunktries; }
	void SetSaturate(int saturate)							{ m_saturate = saturate; }
	
	void SetImportingType(bool safe)						{ m_useSafeImporting = safe; }
	void SetSummary(const char* summaryFilename)			{ m_summaryFilename = summaryFilename; }
	void SetReuse(ReuseType type, const char* filename)		{ m_reuseType = type;	m_reuseFilename = filename; }
	void SetTruncateFloats(bool enabled)					{ m_truncateFloats = enabled; }
	void SetTruncateBits(int bits)							{ m_truncateBits = bits; }
	void SetOverrideAlignments(bool enabled)				{ m_overrideAlignments = enabled; }
	void SetAlignmentBits(int bits)							{ m_alignmentBits = bits; }
	void SetRunInitializers(bool enabled)					{ m_runInitializers = enabled; }
	void SetLargeAddressAware(int enabled)					{ m_largeAddressAware = enabled; }
	void SetTransform(Transform* transform)					{ m_transform = transform; }
	void SetPrintFlags(int printFlags)						{ m_printFlags = printFlags; }

	void PrintOptions(FILE *out);
};

#endif
