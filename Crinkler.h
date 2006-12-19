#ifndef _CRINKLER_H_
#define _CRINKLER_H_

#include <list>
#include "MultiLoader.h"
#include "HunkList.h"
#include "Compressor/Compressor.h"

#include <string>

class HunkLoader;

enum SubsystemType {SUBSYSTEM_CONSOLE, SUBSYSTEM_WINDOWS};

#define VERBOSE_LABELS		1
#define VERBOSE_IMPORTS		2
#define VERBOSE_MODELS		4
#define VERBOSE_FUNCTIONS	8

#define CRINKLER_TITLE			"Crinkler 0.6 (" __DATE__ ") (c) Aske Simonsen & Rune Stubbe 2005-2006"
#define CRINKLER_LINKER_VERSION	"06"

#define TRANSFORM_CALLS		0x01

class Crinkler {
	MultiLoader				m_hunkLoader;
	HunkList				m_hunkPool;
	std::string				m_entry;
	SubsystemType			m_subsytem;
	int						m_imageBase;
	int						m_hashsize;
	int						m_hashtries;
	int						m_verboseFlags;
	bool					m_useSafeImporting;
	CompressionType			m_compressionType;
	std::list<std::string>	m_rangeDlls;
	
	Symbol* getEntrySymbol() const;
	Symbol* findUndecoratedSymbol(const char* name) const;

public:
	Crinkler();
	~Crinkler();

	void load(const char* filename);
	void load(const char* data, int size, const char* module);

	void link(const char* filename);

	Crinkler* setEntry(const char* entry);
	Crinkler* setVerboseFlags(int verboseFlags);
	Crinkler* setSubsystem(SubsystemType subsystem);
	Crinkler* setHashsize(int hashsize);
	Crinkler* setCompressionType(CompressionType compressionType);
	Crinkler* setImportingType(bool safe);
	Crinkler* setHashtries(int hashtries);
	Crinkler* addRangeDll(const char* dllname);
	Crinkler* clearRangeDlls();
	Crinkler* addRangeDlls(std::list<std::string>& dllnames);

};

#endif