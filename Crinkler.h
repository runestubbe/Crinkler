#pragma once
#ifndef _CRINKLER_H_
#define _CRINKLER_H_

#include <map>
#include <string>

#include "MultiLoader.h"
#include "HunkList.h"
#include "Compressor/Compressor.h"
#include "Transform.h"

class HunkLoader;

enum SubsystemType {SUBSYSTEM_CONSOLE, SUBSYSTEM_WINDOWS};

#define VERBOSE_LABELS				1
#define VERBOSE_IMPORTS				2
#define VERBOSE_MODELS				4
#define VERBOSE_FUNCTIONS			8
#define VERBOSE_FUNCTIONS_BYSIZE	16
#define VERBOSE_FUNCTIONS_BYNAME	32

#define CRINKLER_TITLE			"Crinkler 1.0b (" __DATE__ ") (c) 2005-2007 Aske Simon Christensen & Rune Stubbe"
#define CRINKLER_LINKER_VERSION	0x3031

class Crinkler {
	MultiLoader				m_hunkLoader;
	HunkList				m_hunkPool;
	std::string				m_entry;
	SubsystemType			m_subsytem;
	int						m_imageBase;
	int						m_hashsize;
	int						m_hashtries;
	int						m_hunktries;
	int						m_verboseFlags;
	bool					m_useSafeImporting;
	CompressionType			m_compressionType;
	std::vector<std::string>	m_rangeDlls;
	std::map<std::string, std::string>	m_replaceDlls;
	bool					m_showProgressBar;
	Transform*				m_transform;
	int						m_modelbits;
	bool					m_1KMode;
	
	Symbol* getEntrySymbol() const;
	Symbol* findUndecoratedSymbol(const char* name) const;
	void replaceDlls(HunkList& hunkslist);

public:
	Crinkler();
	~Crinkler();

	void load(const char* filename);
	void load(const char* data, int size, const char* module);

	void link(const char* filename);

	Crinkler* set1KMode(bool use1KMode);
	Crinkler* setEntry(const char* entry);
	Crinkler* setVerboseFlags(int verboseFlags);
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

	Crinkler* setTransform(Transform* transform);
};

#endif
