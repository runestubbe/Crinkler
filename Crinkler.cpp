#include "Crinkler.h"
#include "Compressor/Compressor.h"

#include <windows.h>
#include <list>
#include <ctime>

#include "HunkList.h"
#include "Hunk.h"
#include "CoffObjectLoader.h"
#include "CoffLibraryLoader.h"
#include "ImportHandler.h"
#include "Log.h"
#include "HeuristicHunkSorter.h"
#include "EmpiricalHunkSorter.h"
#include "misc.h"
#include "ConsoleProgressBar.h"
#include "WindowProgressBar.h"
#include "CompositeProgressBar.h"
#include "CompressionSummary.h"
#include "data.h"

using namespace std;
const int CRINKLER_VERSION = 0x3630;

Crinkler::Crinkler() {
	m_subsytem = SUBSYSTEM_WINDOWS;
	m_imageBase = 0x00400000;
	m_hashsize = 50*1024*1024;
	m_compressionType = COMPRESSION_FAST;
	m_useSafeImporting = false;
	m_hashtries = 10;
	m_verboseFlags = 0;
}


Crinkler::~Crinkler() {
}


void Crinkler::load(const char* filename) {
	HunkList* hunkList = m_hunkLoader.loadFromFile(filename);
	if(hunkList) {
		m_hunkPool.append(hunkList);
		delete hunkList;
	} else {
		Log::error(0, "", "failed to load file %s", filename);
	}
}

void Crinkler::load(const char* data, int size, const char* module) {
	HunkList* hunkList = m_hunkLoader.load(data, size, module);
	m_hunkPool.append(hunkList);
	delete hunkList;
}

Symbol* Crinkler::findUndecoratedSymbol(const char* name) const {
	Symbol* s = m_hunkPool.findUndecoratedSymbol(name);
	if(s)
		return s;
	else {
		Log::error(0, "", "could not find symbol %s", name);
		return NULL;
	}
}


Symbol* Crinkler::getEntrySymbol() const {
	if(m_entry.size() == 0) {
		switch(m_subsytem) {
			case SUBSYSTEM_CONSOLE:
				return findUndecoratedSymbol("mainCRTStartup");
			case SUBSYSTEM_WINDOWS:
				return findUndecoratedSymbol("WinMainCRTStartup");
		}
		return NULL;
	} else {
		return findUndecoratedSymbol(m_entry.c_str());
	}
}

int getPreciseTime() {
	LARGE_INTEGER time;
	LARGE_INTEGER freq;
	QueryPerformanceCounter(&time);
	QueryPerformanceFrequency(&freq);
	return (int)(time.QuadPart*1000 / freq.QuadPart);
}


int previousPrime(int n) {
in:
	n = (n-2)|1;
	for (int i = 3 ; i*i < n ; i += 2) {
		if (n/i*i == n) goto in;
	}
	return n;
}

void verboseLabels(CompressionSummaryRecord* csr) {
	if(csr->type == RECORD_ROOT) {
		printf("\nlabel name                                   pos comp-pos      size compsize");
	} else {
		if(csr->type == RECORD_SECTION)
			printf("\n%-38.38s", csr->name.c_str());
		
		else if(csr->type == RECORD_PUBLIC)
			printf("  %-36.36s", csr->name.c_str());
		else if(csr->type == RECORD_LOCAL)
			printf("    %-34.34s", csr->name.c_str());


		if(csr->compressedPos >= 0)
			printf(" %9d %8.2f %9d %8.2f\n", csr->pos, csr->compressedPos / (BITPREC*8.0f), csr->size, csr->compressedSize / (BITPREC*8.0f));
		else
			printf(" %9d          %9d\n", csr->pos, csr->size);
	}

	for(vector<CompressionSummaryRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++)
		verboseLabels(*it);
}

void Crinkler::link(const char* filename) {
	int best_hashsize = m_hashsize;
	//place entry point in the beginning
	Symbol* entry = getEntrySymbol();
	if(entry == NULL) {
		Log::error(0, "", "could not find entry symbol");
		return;
	}

	//ensure 1byte aligned entry point
	if(entry->hunk->getAlignmentBits() > 0) {
		Log::warning(0, "", "entry point hunk has alignment greater than 1, forcing alignment of 1");
		entry->hunk->setAlignmentBits(0);
	}
	
	//add a jump to the entry point, if the entry point is not at the beginning of a hunk
	if(entry->value > 0) {
		Log::warning(0, "", "Could not move entry point to beginning of code, inserted jump");
		unsigned char jumpCode[5] = {0xE9, 0x00, 0x00, 0x00, 0x00};
		Hunk* jumpHunk = new Hunk("jump_to_entry_point", (char*)jumpCode, HUNK_IS_CODE, 0, 5, 5);
		m_hunkPool.addHunkFront(jumpHunk);
		relocation r = {entry->name.c_str(), 1, RELOCTYPE_REL32};
		jumpHunk->addRelocation(r);
		jumpHunk->fixate();
	}

	m_hunkPool.removeHunk(entry->hunk);
	m_hunkPool.addHunkFront(entry->hunk);
	entry->hunk->fixate();

	//do imports
	if(m_useSafeImporting)
		load(importSafeObj, importSafeObj_end - importSafeObj, "crinkler import");
	else
		load(importObj, importObj_end - importObj, "crinkler import");

	Symbol* import = findUndecoratedSymbol("Import");
	m_hunkPool.removeHunk(import->hunk);
	m_hunkPool.addHunkFront(import->hunk);
	import->hunk->fixate();
	import->hunk->addSymbol(new Symbol("_ImageBase", m_imageBase, 0, import->hunk));

	{	//check dependencies and remove unused hunks
		list<Hunk*> startHunks;
		startHunks.push_back(entry->hunk);
		startHunks.push_back(import->hunk);
		m_hunkPool.removeUnreferencedHunks(startHunks);
	}

	//handle imports
	HunkList* headerHunks = m_hunkLoader.load(headerObj, headerObj_end - headerObj, "crinkler header");//m_hunkLoader.loadFromFile("modules/header5.obj");
	Hunk* header = headerHunks->findSymbol("_header")->hunk;
	headerHunks->removeHunk(header);
	{	//add imports
		HunkList* importHunkList = ImportHandler::createImportHunks(&m_hunkPool, header, m_rangeDlls, m_verboseFlags & VERBOSE_IMPORTS);
		m_hunkPool.removeImportHunks();
		m_hunkPool.append(importHunkList);
		delete importHunkList;
	}
	HeuristicHunkSorter::sortHunkList(&m_hunkPool);
	int sectionSize = 0x10000;


	//create phase 1 data hunk
	int splittingPoint;
	Hunk* phase1 = m_hunkPool.toHunk("linkedHunk", &splittingPoint);
	phase1->relocate(m_imageBase+sectionSize*2);


	//calculate baseprobs
	int baseprob = 10;
	int baseprobs[8];
	for(int i = 0; i < 8; i++)
		baseprobs[i] = baseprob;	//flat baseprob

	int modelskip = 0;
	Hunk* phase1Compressed, *models;
	{
		int maxsize = phase1->getRawSize()*10;	//allocate plenty of memory
		unsigned char* data = new unsigned char[maxsize];
		int* sizefill = new int[phase1->getRawSize()+1];
		int size;

		//Construct composite progress bar
		ConsoleProgressBar consoleBar;
		WindowProgressBar windowBar;
		CompositeProgressBar progressBar;
		progressBar.addProgressBar(&consoleBar);
		progressBar.addProgressBar(&windowBar);

		progressBar.init();

		progressBar.beginTask("Estimating models for code");
		ModelList ml1 = ApproximateModels((unsigned char*)phase1->getPtr(), splittingPoint, baseprobs, &size, &progressBar, false, m_compressionType);
		progressBar.endTask();
		ml1.print();
		int idealsize = size;

		progressBar.beginTask("Estimating models for data");
		ModelList ml2 = ApproximateModels((unsigned char*)phase1->getPtr()+splittingPoint, phase1->getRawSize() - splittingPoint, baseprobs, &size, &progressBar, false, m_compressionType);
		progressBar.endTask();
		ml2.print();
		idealsize += size;
		
		printf("ideal compressed total size: %d\n", idealsize / BITPREC / 8);
/*
		{
			EmpiricalHunkSorter::sortHunkList(&m_hunkPool, ml1, ml2, baseprobs);
			delete phase1;
			Hunk* phase1 = m_hunkPool.toHunk("linkedHunk", &splittingPoint);
			phase1->relocate(m_imageBase+sectionSize*2);
			progressBar.beginTask("Estimating models for code");
			ModelList ml1 = ApproximateModels((unsigned char*)phase1->getPtr(), splittingPoint, baseprobs, &size, &progressBar, false, m_compressionType);
			progressBar.endTask();
			ml1.print();
			int idealsize = size;

			progressBar.beginTask("Estimating models for data");
			ModelList ml2 = ApproximateModels((unsigned char*)phase1->getPtr()+splittingPoint, phase1->getRawSize() - splittingPoint, baseprobs, &size, &progressBar, false, m_compressionType);
			progressBar.endTask();
			ml2.print();
			idealsize += size;

			printf("ideal compressed total size: %d\n", idealsize / BITPREC / 8);
		}
*/

		//hashing time
		progressBar.beginTask("Optimizing hashsize");
		{
			int bestsize = INT_MAX;
			int hashsize = m_hashsize;
			
			for(int i = 0; i < m_hashtries; i++) {
				CompressionStream cs(data, sizefill, maxsize);
				hashsize = previousPrime(hashsize/2)*2;
				cs.Compress((unsigned char*)phase1->getPtr(), splittingPoint, ml1, baseprobs, hashsize, false);
				cs.Compress((unsigned char*)phase1->getPtr() + splittingPoint, phase1->getRawSize() - splittingPoint, ml2, baseprobs, hashsize, true);
				size = cs.close();
				if(size <= bestsize) {
					bestsize = size;
					best_hashsize = hashsize;
				}
				
				progressBar.update(i+1, m_hashtries+1);
			}

			CompressionStream cs(data, NULL, maxsize);
			cs.Compress((unsigned char*)phase1->getPtr(), splittingPoint, ml1, baseprobs, best_hashsize, false);
			cs.Compress((unsigned char*)phase1->getPtr() + splittingPoint, phase1->getRawSize() - splittingPoint, ml2, baseprobs, best_hashsize, true);
			size = cs.close();
			progressBar.update(m_hashtries+1, m_hashtries+1);
			progressBar.endTask();
			printf("real compressed total size: %d\nbytes lost to hashing: %d\n", size, size - idealsize / BITPREC / 8);
		}
		CompressionSummaryRecord* csr = phase1->getCompressionSummary(sizefill, splittingPoint);
		if(m_verboseFlags & VERBOSE_LABELS)
			verboseLabels(csr);


		delete csr;

		
		phase1Compressed = new Hunk("compressed data", (char*)data, 0, 1, size, size);
		delete[] data;
		phase1Compressed->addSymbol(new Symbol("_PackedData", 0, SYMBOL_IS_RELOCATEABLE, phase1Compressed));
		int modelsSize = 16 + ml1.nmodels + ml2.nmodels;
		{
			unsigned char masks1[256];
			unsigned char masks2[256];
			unsigned int w1 = ml1.getMaskList(masks1, false);
			unsigned int w2 = ml2.getMaskList(masks2, true);
			models = new Hunk("models", 0, 0, 1, modelsSize, modelsSize);
			models->addSymbol(new Symbol("_Models", 0, SYMBOL_IS_RELOCATEABLE, models));
			char* ptr = models->getPtr();
			*(unsigned int*)ptr = splittingPoint*8;			ptr += sizeof(unsigned int);
			*(unsigned int*)ptr = w1;						ptr += sizeof(unsigned int);
			for(int m = 0; m < ml1.nmodels; m++)
				*ptr++ = masks1[m];
			*(unsigned int*)ptr = phase1->getRawSize()*8;	ptr += sizeof(unsigned int);
			*(unsigned int*)ptr = w2;						ptr += sizeof(unsigned int);
			for(int m = 0; m < ml2.nmodels; m++)
				*ptr++ = masks2[m];
		}
		modelskip = ml1.nmodels+8;
	}


	HunkList phase2list;
	phase2list.addHunkBack(header);
	Hunk* depacker = headerHunks->findSymbol("_DepackEntry")->hunk;
	headerHunks->removeHunk(depacker);
	phase2list.addHunkBack(depacker);
	phase2list.addHunkBack(models);
	phase2list.addHunkBack(phase1Compressed);
	header->addSymbol(new Symbol("_HashTable", sectionSize*2+phase1->getRawSize(), SYMBOL_IS_RELOCATEABLE, header));

	Hunk* phase2 = phase2list.toHunk("final");
	//add constants
	{
		int virtualSize = max(phase1->getVirtualSize(), phase1->getRawSize()+best_hashsize);
		virtualSize = align(virtualSize, 16);
		phase2->addSymbol(new Symbol("_HashTableSize", best_hashsize/2, 0, phase2));
		phase2->addSymbol(new Symbol("_UnpackedData", m_imageBase + sectionSize*2, 0, phase2));
		phase2->addSymbol(new Symbol("_VirtualSize", virtualSize, 0, phase2));
		phase2->addSymbol(new Symbol("_ImageBase", m_imageBase, 0, phase2));

		*(phase2->getPtr() + phase2->findSymbol("_BaseProbPtr")->value) = baseprob;
		*(phase2->getPtr() + phase2->findSymbol("_ModelSkipPtr")->value) = modelskip;
		*(phase2->getPtr() + phase2->findSymbol("_SubsystemTypePtr")->value) = m_subsytem == SUBSYSTEM_WINDOWS ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI;
		*((short*)(phase2->getPtr() + phase2->findSymbol("_LinkerVersionPtr")->value)) = CRINKLER_VERSION;
	}
	phase2->relocate(m_imageBase);

	//TODO: safe open this at the very beginning
	FILE* outfile;
	if(fopen_s(&outfile, filename, "wb")) {
		Log::error(0, "", "could not open %s for writing", filename);
		return;
	}
	fwrite(phase2->getPtr(), 1, phase2->getRawSize(), outfile);
	fclose(outfile);

	delete headerHunks;
	delete phase1;
	delete phase2;
}


Crinkler* Crinkler::setEntry(const char* entry) {
	m_entry = entry;
	return this;
}


Crinkler* Crinkler::setSubsystem(SubsystemType subsystem) {
	m_subsytem = subsystem;
	return this;
}

Crinkler* Crinkler::setHashsize(int hashsize) {
	m_hashsize = hashsize*1024*1024;
	return this;
}

Crinkler* Crinkler::setCompressionType(CompressionType compressionType) {
	m_compressionType = compressionType;
	return this;
}

Crinkler* Crinkler::setImportingType(bool safe) {
	m_useSafeImporting = safe;
	return this;
}

Crinkler* Crinkler::addRangeDll(const char* dllname) {
	m_rangeDlls.push_back(dllname);
	return this;
}

Crinkler* Crinkler::addRangeDlls(list<string>& dllnames) {
	m_rangeDlls.splice(m_rangeDlls.end(), dllnames);
	return this;
}

Crinkler* Crinkler::clearRangeDlls() {
	m_rangeDlls.clear();
	return this;
}

Crinkler* Crinkler::setHashtries(int hashtries) {
	m_hashtries = hashtries;
	return this;
}

Crinkler* Crinkler::setVerboseFlags(int verboseFlags) {
	m_verboseFlags = verboseFlags;
	return this;
}