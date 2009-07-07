#pragma once
#ifndef _CRINKLER_H_
#define _CRINKLER_H_

#include <map>
#include <string>
#include <cstdio>

#include "MultiLoader.h"
#include "HunkList.h"
#include "Compressor/Compressor.h"
#include "Transform.h"
#include "StringMisc.h"
#include "ConsoleProgressBar.h"
#include "WindowProgressBar.h"
#include "CompositeProgressBar.h"



class HunkLoader;

const int CRINKLER_IMAGEBASE = 0x400000;
const int CRINKLER_SECTIONSIZE = 0x10000;
const int CRINKLER_SECTIONBASE = CRINKLER_IMAGEBASE+CRINKLER_SECTIONSIZE;
const int CRINKLER_CODEBASE = CRINKLER_IMAGEBASE+2*CRINKLER_SECTIONSIZE;
const int CRINKLER_BASEPROB = 10;

enum SubsystemType {SUBSYSTEM_CONSOLE, SUBSYSTEM_WINDOWS};

const int PRINT_LABELS =			1;
const int PRINT_IMPORTS =			2;
const int PRINT_MODELS =			4;

#define CRINKLER_TITLE "Crinkler 1.2 (" __DATE__ ") (c) 2005-2009 Aske Simon Christensen & Rune Stubbe"
#define CRINKLER_WITH_VERSION "Crinkler 1.2"
const int CRINKLER_LINKER_VERSION = 0x3231;
#define INCLUDE_1K_PACKER

class Crinkler {
	MultiLoader				m_hunkLoader;
	HunkList				m_hunkPool;
	std::string				m_entry;
	std::string				m_summaryFilename;
	SubsystemType			m_subsystem;
	int						m_hashsize;
	int						m_hashtries;
	int						m_hunktries;
	int						m_printFlags;
	bool					m_useSafeImporting;
	CompressionType			m_compressionType;
	std::vector<std::string>	m_rangeDlls;
	std::map<std::string, std::string>	m_replaceDlls;
	bool					m_showProgressBar;
	Transform*				m_transform;
	bool					m_1KMode;
	bool					m_truncateFloats;
	int						m_truncateBits;
	ModelList				m_modellist1;
	ModelList				m_modellist2;

	ConsoleProgressBar		m_consoleBar;
	WindowProgressBar		m_windowBar;
	CompositeProgressBar	m_progressBar;

	Symbol*	moveEntryPointToFront();
	void removeUnreferencedHunks(Hunk* base);
	std::string getEntrySymbolName() const;
	void replaceDlls(HunkList& hunkslist);

	void compress1K(Hunk* phase1, const char* filename, FILE* outfile);

	void loadImportCode(bool useSafeImporting, bool useRangeImport);
	Hunk* createModelHunk(int splittingPoint, int rawsize);
	void initProgressBar();
	void deinitProgressBar();

	int optimizeHashsize(unsigned char* data, int datasize, int hashsize, int splittingPoint, int tries);
	int estimateModels(unsigned char* data, int datasize, int splittingPoint, bool reestimate);
	void setHeaderConstants(Hunk* header, int rawsize, int hashsize, int subsystem_version);

public:
	Crinkler();
	~Crinkler();

	void load(const char* filename);
	void load(const char* data, int size, const char* module);
	void recompress(const char* input_filename, const char* output_filename);
	

	void link(const char* filename);


	
	void addRangeDll(const char* dllname)					{ m_rangeDlls.push_back(dllname); }
	void addReplaceDll(const char* dll1, const char* dll2)	{ m_replaceDlls.insert(make_pair(toLower(dll1), toLower(dll2))); }
	void clearRangeDlls()									{ m_rangeDlls.clear(); }
	void showProgressBar(bool show)							{ m_showProgressBar = show; }

	void set1KMode(bool use1KMode)							{ m_1KMode = use1KMode; }

	void setEntry(const char* entry)						{ m_entry = entry; }
	void setSubsystem(SubsystemType subsystem)				{ m_subsystem = subsystem; }

	void setCompressionType(CompressionType compressionType){ m_compressionType = compressionType; }
	void setHashsize(int hashsize)							{ m_hashsize = hashsize*1024*1024; }
	void setHashtries(int hashtries)						{ m_hashtries = hashtries; }
	void setHunktries(int hunktries)						{ m_hunktries = hunktries; }
	
	void setImportingType(bool safe)						{ m_useSafeImporting = safe; }
	void setSummary(const char* summaryFilename)			{ m_summaryFilename = summaryFilename; }
	void setTruncateFloats(bool enabled)					{ m_truncateFloats = enabled; }
	void setTruncateBits(int bits)							{ m_truncateBits = bits; }
	void setTransform(Transform* transform)					{ m_transform = transform; }
	void setPrintFlags(int printFlags)						{ m_printFlags = printFlags; }

	void printOptions(FILE *out);
};

#endif
