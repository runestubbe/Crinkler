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

class HunkLoader;

const int CRINKLER_IMAGEBASE = 0x400000;
const int CRINKLER_SECTIONSIZE = 0x10000;
const int CRINKLER_SECTIONBASE = CRINKLER_IMAGEBASE+CRINKLER_SECTIONSIZE;
const int CRINKLER_CODEBASE = CRINKLER_IMAGEBASE+2*CRINKLER_SECTIONSIZE;

enum SubsystemType {SUBSYSTEM_CONSOLE, SUBSYSTEM_WINDOWS};

const int PRINT_LABELS =			1;
const int PRINT_IMPORTS =			2;
const int PRINT_MODELS =			4;

#define CRINKLER_TITLE "Crinkler 1.1a (" __DATE__ ") (c) 2005-2009 Aske Simon Christensen & Rune Stubbe"
#define CRINKLER_WITH_VERSION "Crinkler 1.1a"
const int CRINKLER_LINKER_VERSION = 0x3131;
//#define INCLUDE_1K_PACKER

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
	int						m_modelbits;
	bool					m_1KMode;
	bool					m_truncateFloats;
	int						m_truncateBits;
	
	std::string getEntrySymbolName() const;
	void replaceDlls(HunkList& hunkslist);

public:
	Crinkler();
	~Crinkler();

	void load(const char* filename);
	void load(const char* data, int size, const char* module);

	void link(const char* filename);

	Crinkler* set1KMode(bool use1KMode);
	Crinkler* setEntry(const char* entry);
	Crinkler* setPrintFlags(int printFlags);
	Crinkler* setSubsystem(SubsystemType subsystem);
	Crinkler* setHashsize(int hashsize);
	Crinkler* setHunktries(int hunktries);
	Crinkler* setCompressionType(CompressionType compressionType);
	Crinkler* setImportingType(bool safe);
	Crinkler* setHashtries(int hashtries);
	Crinkler* addRangeDll(const char* dllname);
	Crinkler* addReplaceDll(const char* dll1, const char* dll2);
	Crinkler* clearRangeDlls();
	Crinkler* showProgressBar(bool show);
	Crinkler* setModelBits(int modelbits);
	Crinkler* setSummary(const char* summaryFilename);
	Crinkler* setTruncateFloats(bool enabled);
	Crinkler* setTruncateBits(int bits);
	Crinkler* setTransform(Transform* transform);

	void printOptions(FILE *out);
};

#endif
