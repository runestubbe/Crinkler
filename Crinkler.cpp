#include "Crinkler.h"
#include "Compressor/Compressor.h"
#include "TinyCompress.h"
#include "Fix.h"

#include <list>
#include <set>
#include <ctime>
#include <fstream>

#include "HunkList.h"
#include "Hunk.h"
#include "CoffObjectLoader.h"
#include "CoffLibraryLoader.h"
#include "ImportHandler.h"
#include "Log.h"
#include "HeuristicHunkSorter.h"
#include "EmpiricalHunkSorter.h"
#include "misc.h"
#include "data.h"
#include "Symbol.h"
#include "HtmlReport.h"
#include "NameMangling.h"
#include "MemoryFile.h"

using namespace std;

Crinkler::Crinkler():
	m_subsystem(SUBSYSTEM_WINDOWS),
	m_hashsize(100*1024*1024),
	m_compressionType(COMPRESSION_FAST),
	m_useSafeImporting(false),
	m_hashtries(0),
	m_hunktries(0),
	m_printFlags(0),
	m_showProgressBar(false),
	m_1KMode(false),
	m_summaryFilename(""),
	m_truncateFloats(false),
	m_truncateBits(32),
	m_overrideAlignments(false),
	m_alignmentBits(0)
{
	m_modellist1 = InstantModels();
	m_modellist2 = InstantModels();
}


Crinkler::~Crinkler() {
}

void Crinkler::replaceDlls(HunkList& hunklist) {
	set<string> usedDlls;
	//replace dll
	for(int i = 0; i < hunklist.getNumHunks(); i++) {
		Hunk* hunk = hunklist[i];
		if(hunk->getFlags() & HUNK_IS_IMPORT) {
			map<string, string>::iterator it = m_replaceDlls.find(toLower(hunk->getImportDll()));
			if(it != m_replaceDlls.end()) {
				hunk->setImportDll(it->second.c_str());
				usedDlls.insert(it->first);
			}
		}
	}

	//warn about unused replace DLLs
	for(map<string, string>::iterator it = m_replaceDlls.begin(); it != m_replaceDlls.end(); it++) {
		if(usedDlls.find(it->first) == usedDlls.end()) {
			Log::warning("", "No functions were imported from replaced dll '%s'", it->first.c_str());
		}
	}
}


void Crinkler::overrideAlignments(HunkList& hunklist) {
	for(int i = 0; i < hunklist.getNumHunks(); i++) {
		Hunk* hunk = hunklist[i];
		hunk->overrideAlignment(m_alignmentBits);
	}
}

void Crinkler::load(const char* filename) {
	HunkList* hunkList = m_hunkLoader.loadFromFile(filename);
	if(hunkList) {
		m_hunkPool.append(hunkList);
		delete hunkList;
	} else {
		Log::error(filename, "Unsupported file type");
	}
}

void Crinkler::load(const char* data, int size, const char* module) {
	HunkList* hunkList = m_hunkLoader.load(data, size, module);
	m_hunkPool.append(hunkList);
	delete hunkList;
}

std::string Crinkler::getEntrySymbolName() const {
	if(m_entry.empty()) {
		switch(m_subsystem) {
			case SUBSYSTEM_CONSOLE:
				return "mainCRTStartup";
			case SUBSYSTEM_WINDOWS:
				return "WinMainCRTStartup";
		}
		return "";
	}
	return m_entry;
}

Symbol*	Crinkler::findEntryPoint() {
	//place entry point in the beginning
	string entryName = getEntrySymbolName();
	Symbol* entry = m_hunkPool.findUndecoratedSymbol(entryName.c_str());
	if(entry == NULL) {
		Log::error("", "Cannot find entry point '%s'. See manual for details.", entryName.c_str());
		return NULL;
	}

	//1byte aligned entry point
	if(entry->hunk->getAlignmentBits() > 0) {
		Log::warning("", "Entry point hunk has alignment greater than 1, forcing alignment of 1");
		entry->hunk->setAlignmentBits(0);
	}

	if(entry->value > 0) {
		Log::warning("", "Entry point not at start of section, jump necessary");
	}

	return entry;
}

void Crinkler::removeUnreferencedHunks(Hunk* base) {
	//check dependencies and remove unused hunks
	list<Hunk*> startHunks;
	startHunks.push_back(base);

	//hack to ensure that LoadLibrary & MessageBox is there to be used in the import code
	Symbol* loadLibrary = m_hunkPool.findSymbol("__imp__LoadLibraryA@4"); 
	Symbol* messageBox = m_hunkPool.findSymbol("__imp__MessageBoxA@16");
	if(loadLibrary != NULL)
		startHunks.push_back(loadLibrary->hunk);
	if(m_useSafeImporting && messageBox != NULL)
		startHunks.push_back(messageBox->hunk);

	m_hunkPool.removeUnreferencedHunks(startHunks);
}


static int getPreciseTime() {
	LARGE_INTEGER time;
	LARGE_INTEGER freq;
	QueryPerformanceCounter(&time);
	QueryPerformanceFrequency(&freq);
	return (int)(time.QuadPart*1000 / freq.QuadPart);
}


static int previousPrime(int n) {
in:
	n = (n-2)|1;
	for (int i = 3 ; i*i < n ; i += 2) {
		if (n/i*i == n) goto in;
	}
	return n;
}

void verboseLabels(CompressionReportRecord* csr) {
	if(csr->type & RECORD_ROOT) {
		printf("\nlabel name                                   pos comp-pos      size compsize");
	} else {
		string strippedName = stripCrinklerSymbolPrefix(csr->name.c_str());
		if(csr->type & RECORD_SECTION)
			printf("\n%-38.38s", strippedName.c_str());
		else if(csr->type & RECORD_OLD_SECTION)
			printf("  %-36.36s", strippedName.c_str());
		else if(csr->type & RECORD_PUBLIC)
			printf("    %-34.34s", strippedName.c_str());
		else
			printf("      %-32.32s", strippedName.c_str());

		if(csr->compressedPos >= 0)
			printf(" %9d %8.2f %9d %8.2f\n", csr->pos, csr->compressedPos / (BITPREC*8.0f), csr->size, csr->compressedSize / (BITPREC*8.0f));
		else
			printf(" %9d          %9d\n", csr->pos, csr->size);
	}

	for(vector<CompressionReportRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++)
		verboseLabels(*it);
}

#ifdef INCLUDE_1K_PACKER
void Crinkler::compress1K(Hunk* phase1, const char* filename, FILE* outfile) {
	int maxsize = phase1->getRawSize()*2+1000;	//allocate plenty of memory
	Hunk* header = phase1->findSymbol("_header")->hunk;

	int compressed_size = 1024*1024;
	unsigned char* compressed_data = new unsigned char[compressed_size];
	char* ptr = phase1->getPtr();
	if(ptr[phase1->getRawSize()-1] != 0) {
		phase1->appendZeroes(1);
	}

	int* sizefill = new int[maxsize];

	int baseprob0;
	int baseprob1;
	int boostfactor;
	unsigned int modelmask;
	TinyCompress((unsigned char*)phase1->getPtr(), phase1->getRawSize(), compressed_data, compressed_size,
		boostfactor, baseprob0, baseprob1, modelmask, sizefill);
	Hunk* phase1Compressed = new Hunk("compressed data", (char*)compressed_data, 0, 0, compressed_size, compressed_size);
	phase1Compressed->addSymbol(new Symbol("_PackedData", 0, SYMBOL_IS_RELOCATEABLE, phase1Compressed));
	delete[] compressed_data;
	printf("compressed size: %d\n", compressed_size);

	HunkList phase2list;
	phase2list.addHunkBack(header);
	phase2list.addHunkBack(phase1Compressed);

	Hunk* phase2 = phase2list.toHunk("final", CRINKLER_IMAGEBASE);
	//add constants
	{
		int virtualSize = align(phase1->getVirtualSize(), 16);
		int packedDataPos = phase2->findSymbol("_PackedData")->value;
		int packedDataOffset = (packedDataPos - CRINKLER_SECTIONSIZE*2)*8;
		printf("packed data offset: %x\n", packedDataOffset);
		printf("image size: %x\n", phase1->getRawSize());
		phase2->addSymbol(new Symbol("_UnpackedData", CRINKLER_CODEBASE, 0, phase2));
		phase2->addSymbol(new Symbol("_DepackEndPosition", CRINKLER_CODEBASE+phase1->getRawSize(), 0, phase2));
		phase2->addSymbol(new Symbol("_VirtualSize", virtualSize, 0, phase2));
		phase2->addSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, phase2));
		phase2->addSymbol(new Symbol("_ModelMask", modelmask, 0, phase2));

		*(phase2->getPtr() + phase2->findSymbol("_SubsystemTypePtr")->value) = m_subsystem == SUBSYSTEM_WINDOWS ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI;
		*(phase2->getPtr() + phase2->findSymbol("_BaseProbPtr0")->value) = baseprob0;
		*(phase2->getPtr() + phase2->findSymbol("_BaseProbPtr1")->value) = baseprob1;
		*(phase2->getPtr() + phase2->findSymbol("_BoostFactorPtr")->value) = boostfactor;
	}
	phase2->relocate(CRINKLER_IMAGEBASE);

	fwrite(phase2->getPtr(), 1, phase2->getRawSize(), outfile);
	fclose(outfile);

	printf("\nFinal file size: %d\n\n", phase2->getRawSize());

	CompressionReportRecord* csr = phase1->getCompressionSummary(sizefill, phase1->getRawSize());
	if(m_printFlags & PRINT_LABELS)
		verboseLabels(csr);
	if(!m_summaryFilename.empty())
		htmlReport(csr, m_summaryFilename.c_str(), *phase1, *phase1, sizefill, filename, this);
	delete csr;
	delete[] sizefill;

	delete phase1;
	delete phase2;
}
#endif

void Crinkler::loadImportCode(bool useSafeImporting, bool useRangeImport) {
	//do imports
	if(m_1KMode) {
		load(import1KObj, import1KObj_end - import1KObj, "Crinkler import");
	} else {
		if(useSafeImporting)
			if(useRangeImport)
				load(importSafeRangeObj, importSafeRangeObj_end - importSafeRangeObj, "Crinkler import");
			else
				load(importSafeObj, importSafeObj_end - importSafeObj, "Crinkler import");
		else
			if(useRangeImport)
				load(importRangeObj, importRangeObj_end - importRangeObj, "Crinkler import");
			else
				load(importObj, importObj_end - importObj, "Crinkler import");
	}
}

static void notCrinklerFileError() {
	Log::error("", "Input file is not a Crinkler compressed executable");
}

Hunk* Crinkler::createModelHunk(int splittingPoint, int rawsize) {
	Hunk* models;
	int modelsSize = 16 + m_modellist1.nmodels + m_modellist2.nmodels;
	unsigned char masks1[256];
	unsigned char masks2[256];
	unsigned int w1 = m_modellist1.getMaskList(masks1, false);
	unsigned int w2 = m_modellist2.getMaskList(masks2, true);
	models = new Hunk("models", 0, 0, 1, modelsSize, modelsSize);
	models->addSymbol(new Symbol("_Models", 0, SYMBOL_IS_RELOCATEABLE, models));
	char* ptr = models->getPtr();
	*(unsigned int*)ptr = CRINKLER_CODEBASE+splittingPoint;			ptr += sizeof(unsigned int);
	*(unsigned int*)ptr = w1;						ptr += sizeof(unsigned int);
	for(int m = 0; m < m_modellist1.nmodels; m++)
		*ptr++ = masks1[m];
	*(unsigned int*)ptr = CRINKLER_CODEBASE+rawsize;	ptr += sizeof(unsigned int);
	*(unsigned int*)ptr = w2;						ptr += sizeof(unsigned int);
	for(int m = 0; m < m_modellist2.nmodels; m++)
		*ptr++ = masks2[m];
	return models;
}

int Crinkler::optimizeHashsize(unsigned char* data, int datasize, int hashsize, int splittingPoint, int tries) {
	if(tries == 0)
		return hashsize;

	int maxsize = datasize*2+1000;
	unsigned char* buff = new unsigned char[maxsize];
	int bestsize = INT_MAX;
	int best_hashsize = hashsize;
	m_progressBar.beginTask("Optimizing hash table size");

	for(int i = 0; i < tries; i++) {
		CompressionStream cs(buff, NULL, maxsize);
		hashsize = previousPrime(hashsize/2)*2;
		cs.Compress(data, splittingPoint, m_modellist1, CRINKLER_BASEPROB, hashsize, true, false);
		cs.Compress(data + splittingPoint, datasize - splittingPoint, m_modellist2, CRINKLER_BASEPROB, hashsize, false, true);

		int size = cs.Close();
		if(size <= bestsize) {
			bestsize = size;
			best_hashsize = hashsize;
		}

		m_progressBar.update(i+1, m_hashtries);
	}
	m_progressBar.endTask();
	
	delete[] buff;
	return best_hashsize;
}

int Crinkler::estimateModels(unsigned char* data, int datasize, int splittingPoint, bool reestimate) {
	int size1, size2;
	m_progressBar.beginTask(reestimate ? "Reestimating models for code" : "Estimating models for code");
	m_modellist1 = ApproximateModels(data, splittingPoint, CRINKLER_BASEPROB, &size1, &m_progressBar, m_printFlags & PRINT_MODELS, m_compressionType);
	m_progressBar.endTask();

	m_progressBar.beginTask(reestimate ? "Reestimating models for data" : "Estimating models for data");
	m_modellist2 = ApproximateModels(data+splittingPoint, datasize - splittingPoint, CRINKLER_BASEPROB, &size2, &m_progressBar, m_printFlags & PRINT_MODELS, m_compressionType);
	m_progressBar.endTask();

	int idealsize = size1+size2;
	printf(reestimate ? "\nReestimated ideal compressed total size: %d\n" :
						"\nEstimated ideal compressed total size: %d\n", 
						idealsize / BITPREC / 8);
	return idealsize;
}

void Crinkler::setHeaderConstants(Hunk* header, Hunk* phase1, int hashsize, int subsystem_version) {
	int virtualSize = align(max(phase1->getVirtualSize(), phase1->getRawSize()+hashsize), 16);
	header->addSymbol(new Symbol("_HashTableSize", hashsize/2, 0, header));
	header->addSymbol(new Symbol("_UnpackedData", CRINKLER_CODEBASE, 0, header));
	header->addSymbol(new Symbol("_VirtualSize", virtualSize, 0, header));
	header->addSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, header));

	*(header->getPtr() + header->findSymbol("_BaseProbPtr")->value) = CRINKLER_BASEPROB;
	*(header->getPtr() + header->findSymbol("_ModelSkipPtr")->value) = m_modellist1.nmodels+8;
	*(header->getPtr() + header->findSymbol("_SubsystemTypePtr")->value) = subsystem_version;
	*((short*)(header->getPtr() + header->findSymbol("_LinkerVersionPtr")->value)) = CRINKLER_LINKER_VERSION;
}

void Crinkler::recompress(const char* input_filename, const char* output_filename) {
	MemoryFile file(input_filename);
	unsigned char* indata = (unsigned char*)file.getPtr();
	if (indata == NULL) {
		Log::error("", "Cannot open file '%s'\n", input_filename);
	}


	//open output file now, just to be sure :)
	FILE* outfile;
	if(fopen_s(&outfile, output_filename, "wb")) {
		Log::error("", "Cannot open '%s' for writing", output_filename);
		return;
	}

	int length = file.getSize();

	char majorlv = 0, minorlv = 0;
	if (length >= 200 && *(int *)&indata[60] == 12) {
		majorlv = indata[38];
		minorlv = indata[39];
	} else {
		notCrinklerFileError();
	}

	if (majorlv < '0' || majorlv > '9' ||
		minorlv < '0' || minorlv > '9') {
			notCrinklerFileError();
	}

	if (majorlv == '0' && minorlv == '6') {
		majorlv = '1';
		minorlv = '0';
	}

	printf("File compressed with Crinkler version %c.%c\n", majorlv, minorlv);

	switch(majorlv) {
		case '0':
			switch(minorlv) {
				case '1':
				case '2':
				case '3':
					Log::error("", "Only files compressed using Crinkler 0.4 or newer can be recompressed.\n");
					return;
					break;
				case '4':
				case '5':
					FixMemory((char*)indata);
					break;
			}
			break;
		case '1':
			switch(minorlv) {
				case '0':
					FixMemory((char*)indata);
					break;
			}
			break;
	}

	int virtualSize = (*(int*)&indata[0x5C]) - 0x20000;
	int hashtable_size = -1;
	int return_offset = -1;
	int models_address = -1;
	int depacker_start = -1;
	for(int i = 0; i < 0x200; i++) {
		if(indata[i] == 0xbf && indata[i+5] == 0xb9 && hashtable_size == -1) {
			hashtable_size = (*(int*)&indata[i+6]) * 2;
		}
		if(indata[i] == 0x7B && indata[i+2] == 0xC3 && return_offset == -1) {
			indata[i+2] = 0xCC;
			return_offset = i+2;
		}
		if(indata[i] == 0x4b && indata[i+1] == 0x61 && indata[i+2] == 0x7F) {
			depacker_start = i;
		}
		if(indata[i] == 0xbe && indata[i+3] == 0x40 && indata[i+4] == 0x00) {
			models_address = *(int*)&indata[i+1];
		}
	}
	if(hashtable_size == -1 || return_offset == -1 || depacker_start == -1 || models_address == -1)
		notCrinklerFileError();

	int models_offset = models_address-0x400000;
	unsigned int weightmask1 = *(unsigned int*)&indata[models_offset+4];
	unsigned char* models1 = &indata[models_offset+8];
	m_modellist1.setFromModelsAndMask(models1, weightmask1);
	int modelskip = 8 + m_modellist1.nmodels;
	unsigned int weightmask2 = *(unsigned int*)&indata[models_offset+modelskip+4];
	unsigned char* models2 = &indata[models_offset+modelskip+8];
	m_modellist2.setFromModelsAndMask(models2, weightmask2);

	CompressionType compmode = m_modellist1.detectCompressionType();
	int subsystem_version = indata[0x68];

	printf("Original Virtual size: %d\n", virtualSize);

	printf("Original Subsystem type: %s\n", subsystem_version == 3 ? "CONSOLE" : "WINDOWS");
	printf("Original Compression mode: %s\n", compmode == COMPRESSION_INSTANT ? "INSTANT" : "FAST/SLOW");
	printf("Original Hash size: %d\n", hashtable_size);

	int rawsize = (*(int*)&indata[models_offset+modelskip]) / 8;
	int splittingPoint = (*(int*)&indata[models_offset]) / 8;

	printf("Code size: %d\n", splittingPoint);
	printf("Data size: %d\n", rawsize-splittingPoint);

	STARTUPINFO startupInfo = {0};
	startupInfo.cb = sizeof(startupInfo);

	char tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	char tempFilename[MAX_PATH];

	GetTempFileName(tempPath, "", 0, tempFilename);
	PROCESS_INFORMATION pi;

	if(!file.write(tempFilename)) {
		Log::error("", "Failed to write to temporary file '%s'\n", tempFilename);
	}

	CreateProcess(tempFilename, NULL, NULL, NULL, false, NORMAL_PRIORITY_CLASS|CREATE_SUSPENDED, NULL, NULL, &startupInfo, &pi);
	DebugActiveProcess(pi.dwProcessId);
	ResumeThread(pi.hThread);

	bool done = false;
	do {
		DEBUG_EVENT de;
		if(WaitForDebugEvent(&de, 20000) == 0) {
			Log::error("", "Program was been unresponsive for more than 20 seconds - closing down\n");
		}

		if(de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT && 
			(de.u.Exception.ExceptionRecord.ExceptionAddress == (PVOID)(0x410000+return_offset) ||
			de.u.Exception.ExceptionRecord.ExceptionAddress == (PVOID)(0x400000+return_offset))) {
			done = true;
		}

		if(!done)
			ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
	} while(!done);

	unsigned char* rawdata = new unsigned char[rawsize];
	SIZE_T read;
	if(ReadProcessMemory(pi.hProcess, (LPCVOID)0x420000, rawdata, rawsize, &read) == 0 || read != rawsize) {
		Log::error("", "Failed to read process memory\n");
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	//patch import code
	static const unsigned char old_import_code[] = {0x31, 0xC0, 0x64, 0x8B, 0x40, 0x30, 0x8B, 0x40, 
													0x0C, 0x8B, 0x40, 0x1C, 0x8B, 0x40, 0x00, 0x8B,
													0x68, 0x08};
	static const unsigned char new_import_code[] = {0x64, 0x67, 0x8B, 0x47, 0x30, 0x8B, 0x40, 0x0C, 0x8B, 0x40, 0x0C, 0x8B, 0x00, 0x8B, 0x00, 0x8B, 0x68, 0x18};
	if (memcmp(rawdata+0x0F, old_import_code, sizeof(old_import_code)) == 0) {			//no calltrans
		memcpy(rawdata+0x0F, new_import_code, sizeof(new_import_code));
		printf("Import code successfully patched.\n");
	} else if (memcmp(rawdata+0x27, old_import_code, sizeof(old_import_code)) == 0) {	//with calltrans
		memcpy(rawdata+0x27, new_import_code, sizeof(new_import_code));
		printf("Import code successfully patched.\n");
	} else if (memcmp(rawdata+0x0F, new_import_code, sizeof(new_import_code)) == 0 ||
			   memcmp(rawdata+0x27, new_import_code, sizeof(new_import_code)) == 0)
	{
		printf("Import code does not need patching.\n");
	} else {
		Log::error("", "Cannot find old import code to patch\n");
	}
	
	printf("Recompressing...\n");

	Hunk* phase1 = new Hunk("linked", (char*)rawdata, HUNK_IS_CODE|HUNK_IS_WRITEABLE, 0, rawsize, virtualSize);
	delete[] rawdata;

	int maxsize = phase1->getRawSize()*2+1000;

	int* sizefill = new int[maxsize];

	//calculate baseprobs
	const int baseprob = 10;

	unsigned char* data = new unsigned char[maxsize];
	int best_hashsize;
	Hunk* phase1Compressed;
	int size, idealsize = 0;
	if (m_compressionType < 0)
	{
		// Keep models
		if (m_hashsize < 0) {
			// Use original optimized hash size
			setHashsize((hashtable_size-1)/(1024*1024)+1);
			best_hashsize = hashtable_size;
			setHashtries(0);
		} else {
			best_hashsize = previousPrime(m_hashsize/2)*2;
			initProgressBar();

			//rehashing time
			best_hashsize = optimizeHashsize((unsigned char*)phase1->getPtr(), phase1->getRawSize(), best_hashsize, splittingPoint, m_hashtries);
			deinitProgressBar();
		}
	} else {
		if (m_hashsize < 0) {
			setHashsize((hashtable_size-1)/(1024*1024)+1);
		}
		best_hashsize = previousPrime(m_hashsize/2)*2;
		if (m_compressionType != COMPRESSION_INSTANT) {
			initProgressBar();
			idealsize = estimateModels((unsigned char*)phase1->getPtr(), phase1->getRawSize(), splittingPoint, false);
			
			//hashing time
			best_hashsize = optimizeHashsize((unsigned char*)phase1->getPtr(), phase1->getRawSize(), best_hashsize, splittingPoint, m_hashtries);
			deinitProgressBar();
		}
	}

	CompressionStream cs(data, sizefill, maxsize);
	cs.Compress((unsigned char*)phase1->getPtr(), splittingPoint, m_modellist1, baseprob, best_hashsize, true, false);
	cs.Compress((unsigned char*)phase1->getPtr() + splittingPoint, phase1->getRawSize() - splittingPoint, m_modellist2, baseprob, best_hashsize, false, true);
	size = cs.Close();
	if(m_compressionType != -1 && m_compressionType != COMPRESSION_INSTANT)
		printf("Real compressed total size: %d\nBytes lost to hashing: %d\n", size, size - idealsize / BITPREC / 8);

	setCompressionType(compmode);
	CompressionReportRecord* csr = phase1->getCompressionSummary(sizefill, splittingPoint);
	if(m_printFlags & PRINT_LABELS)
		verboseLabels(csr);
	if(!m_summaryFilename.empty())
		htmlReport(csr, m_summaryFilename.c_str(), *phase1, *phase1, sizefill, output_filename, this);
	delete csr;
	delete[] sizefill;

	phase1Compressed = new Hunk("compressed data", (char*)data, 0, 0, size, size);
	delete[] data;

	phase1Compressed->addSymbol(new Symbol("_PackedData", 0, SYMBOL_IS_RELOCATEABLE, phase1Compressed));
	Hunk* models = createModelHunk(splittingPoint, phase1->getRawSize());

	HunkList phase2list;
	HunkList* headerHunks = m_hunkLoader.load(headerObj, headerObj_end - headerObj, "crinkler header");

	Hunk* header = headerHunks->findSymbol("_header")->hunk;
	headerHunks->removeHunk(header);
	Hunk* depacker = headerHunks->findSymbol("_DepackEntry")->hunk;
	headerHunks->removeHunk(depacker);

	{	//copy hashes from old header
		DWORD* new_header_ptr = (DWORD*)header->getPtr();
		DWORD* old_header_ptr = (DWORD*)indata;

		for(int i = 0; i < depacker_start/4; i++) {
			if(new_header_ptr[i] == 'HSAH')
				new_header_ptr[i] = old_header_ptr[i];
		}
		header->setRawSize(depacker_start);
		header->setVirtualSize(depacker_start);
	}

	phase2list.addHunkBack(header);
	phase2list.addHunkBack(depacker);
	phase2list.addHunkBack(models);
	phase2list.addHunkBack(phase1Compressed);
	header->addSymbol(new Symbol("_HashTable", CRINKLER_SECTIONSIZE*2+phase1->getRawSize(), SYMBOL_IS_RELOCATEABLE, header));

	Hunk* phase2 = phase2list.toHunk("final", CRINKLER_IMAGEBASE);
	if(m_subsystem >= 0) {
		subsystem_version = (m_subsystem == SUBSYSTEM_WINDOWS) ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI;
	}
	setHeaderConstants(phase2, phase1, best_hashsize, subsystem_version);
	phase2->relocate(CRINKLER_IMAGEBASE);

	fwrite(phase2->getPtr(), 1, phase2->getRawSize(), outfile);
	fclose(outfile);

	printf("\nFinal file size: %d\n\n", phase2->getRawSize());

	delete phase1;
	delete phase2;
}

void Crinkler::link(const char* filename) {
	//open output file now, just to be sure :)
	FILE* outfile;
	if(fopen_s(&outfile, filename, "wb")) {
		Log::error("", "Cannot open '%s' for writing", filename);
		return;
	}

	//find old size
	fseek(outfile, 0, SEEK_END);
	int old_filesize = ftell(outfile);
	fseek(outfile, 0, SEEK_SET);

	//find entry hunk and move it to front
	Symbol* entry = findEntryPoint();
	if(entry == NULL)
		return;
	
	//color hunks from entry hunk
	removeUnreferencedHunks(entry->hunk);

	//replace dlls
	replaceDlls(m_hunkPool);

	if (m_overrideAlignments) overrideAlignments(m_hunkPool);

	//load appropriate header
	HunkList* headerHunks = m_1KMode ?	m_hunkLoader.load(header1KObj, header1KObj_end - header1KObj, "crinkler header") :
										m_hunkLoader.load(headerObj, headerObj_end - headerObj, "crinkler header");

	Hunk* header = headerHunks->findSymbol("_header")->hunk;
	headerHunks->removeHunk(header);

	Hunk* depacker = headerHunks->findSymbol("_DepackEntry")->hunk;
	headerHunks->removeHunk(depacker);

	bool usesRangeImport=false;
	{	//add imports
		HunkList* importHunkList = m_1KMode ?	ImportHandler::createImportHunks1K(&m_hunkPool, m_printFlags & PRINT_IMPORTS) :
												ImportHandler::createImportHunks(&m_hunkPool, header, m_rangeDlls, m_printFlags & PRINT_IMPORTS, usesRangeImport);
		m_hunkPool.removeImportHunks();
		m_hunkPool.append(importHunkList);
		delete importHunkList;
	}

	loadImportCode(m_useSafeImporting, usesRangeImport);

	Symbol* import = m_hunkPool.findSymbol("_Import");
	m_hunkPool.removeHunk(import->hunk);
	m_hunkPool.addHunkFront(import->hunk);
	import->hunk->setAlignmentBits(0);
	import->hunk->setContinuation(entry);
	// Make sure import code has access to the _ImageBase address
	import->hunk->addSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, import->hunk));

	//truncate floats
	if(m_truncateFloats) {
		printf("\nTruncating floats:\n");
		m_hunkPool.roundFloats(m_truncateBits);
	}

	//sort hunks heuristically
	HeuristicHunkSorter::sortHunkList(&m_hunkPool);

	//create phase 1 data hunk
	int splittingPoint;
	Hunk* phase1, *phase1Untransformed;
	m_transform->linkAndTransform(&m_hunkPool, import, CRINKLER_CODEBASE, phase1, &phase1Untransformed, &splittingPoint, true);
	int maxsize = phase1->getRawSize()*2+1000;	//allocate plenty of memory	

	printf("\nUncompressed size of code: %5d\n", splittingPoint);
	printf("Uncompressed size of data: %5d\n", phase1->getRawSize() - splittingPoint);

	//Do 1k specific stuff
#ifdef INCLUDE_1K_PACKER
	if(m_1KMode) {
		compress1K(phase1, filename, outfile);
		delete phase1Untransformed;
		return;
	}
#endif

	int* sizefill = new int[maxsize];

	unsigned char* data = new unsigned char[maxsize];
	int best_hashsize = previousPrime(m_hashsize/2)*2;
	Hunk* phase1Compressed;
	int size, idealsize = 0;
	if (m_compressionType != COMPRESSION_INSTANT) {
		initProgressBar();
		idealsize = estimateModels((unsigned char*)phase1->getPtr(), phase1->getRawSize(), splittingPoint, false);

		if(m_hunktries > 0) {
			EmpiricalHunkSorter::sortHunkList(&m_hunkPool, *m_transform, m_modellist1, m_modellist2, CRINKLER_BASEPROB, m_hunktries, m_showProgressBar ? &m_windowBar : NULL);
			delete phase1;
			delete phase1Untransformed;
			m_transform->linkAndTransform(&m_hunkPool, import, CRINKLER_CODEBASE, phase1, &phase1Untransformed, &splittingPoint, true);

			idealsize = estimateModels((unsigned char*)phase1->getPtr(), phase1->getRawSize(), splittingPoint, true);
		}
		
		//hashing time
		best_hashsize = optimizeHashsize((unsigned char*)phase1->getPtr(), phase1->getRawSize(), best_hashsize, splittingPoint, m_hashtries);
		deinitProgressBar();
	}

	CompressionStream cs(data, sizefill, maxsize);
	cs.Compress((unsigned char*)phase1->getPtr(), splittingPoint, m_modellist1, CRINKLER_BASEPROB, best_hashsize, true, false);
	cs.Compress((unsigned char*)phase1->getPtr() + splittingPoint, phase1->getRawSize() - splittingPoint, m_modellist2, CRINKLER_BASEPROB, best_hashsize, false, true);
	size = cs.Close();
	if(m_compressionType != COMPRESSION_INSTANT)
		printf("Real compressed total size: %d\nBytes lost to hashing: %d\n", size, size - idealsize / BITPREC / 8);

	CompressionReportRecord* csr = phase1->getCompressionSummary(sizefill, splittingPoint);
	if(m_printFlags & PRINT_LABELS)
		verboseLabels(csr);
	if(!m_summaryFilename.empty())
		htmlReport(csr, m_summaryFilename.c_str(), *phase1, *phase1Untransformed, sizefill, filename, this);
	delete csr;
	delete[] sizefill;
	
	phase1Compressed = new Hunk("compressed data", (char*)data, 0, 0, size, size);
	delete[] data;
	phase1Compressed->addSymbol(new Symbol("_PackedData", 0, SYMBOL_IS_RELOCATEABLE, phase1Compressed));
	Hunk* models = createModelHunk(splittingPoint, phase1->getRawSize());

	HunkList phase2list;
	phase2list.addHunkBack(header);
	phase2list.addHunkBack(depacker);
	phase2list.addHunkBack(models);
	phase2list.addHunkBack(phase1Compressed);
	header->addSymbol(new Symbol("_HashTable", CRINKLER_SECTIONSIZE*2+phase1->getRawSize(), SYMBOL_IS_RELOCATEABLE, header));

	Hunk* phase2 = phase2list.toHunk("final", CRINKLER_IMAGEBASE);
	//add constants
	setHeaderConstants(phase2, phase1, best_hashsize, m_subsystem == SUBSYSTEM_WINDOWS ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI);
	phase2->relocate(CRINKLER_IMAGEBASE);

	fwrite(phase2->getPtr(), 1, phase2->getRawSize(), outfile);
	fclose(outfile);

	if(old_filesize) {
		printf("\nOverwritten file size: %d\n", old_filesize);
		printf("\nFinal file size: %d  Delta size: %d \n\n", phase2->getRawSize(), old_filesize - phase2->getRawSize());
	} else {
		printf("\nFinal file size: %d\n\n", phase2->getRawSize());
	}	

	delete phase1;
	delete phase1Untransformed;
	delete phase2;
}

void Crinkler::printOptions(FILE *out) {
	fprintf(out, " /SUBSYSTEM:%s", m_subsystem == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
	if (!m_entry.empty()) {
		fprintf(out, " /ENTRY:%s", m_entry.c_str());
	}
	fprintf(out, " /COMPMODE:%s", compTypeName(m_compressionType));
	fprintf(out, " /HASHSIZE:%d", m_hashsize/1048576);
	if (m_compressionType != COMPRESSION_INSTANT) {
		fprintf(out, " /HASHTRIES:%d", m_hashtries);
		fprintf(out, " /ORDERTRIES:%d", m_hunktries);
	}
	for(int i = 0; i < m_rangeDlls.size(); i++) {
		fprintf(out, " /RANGE:%s", m_rangeDlls[i].c_str());
	}
	for(map<string, string>::iterator it = m_replaceDlls.begin(); it != m_replaceDlls.end(); it++) {
		fprintf(out, " /REPLACEDLL:%s=%s", it->first.c_str(), it->second.c_str());
	}
	if (!m_useSafeImporting) {
		fprintf(out, " /UNSAFEIMPORT");
	}
	if (m_transform->getDetransformer() != NULL) {
		// TODO: Make the transform tell its name properly
		fprintf(out, " /TRANSFORM:CALLS");
	}
	if (m_truncateFloats) {
		fprintf(out, " /TRUNCATEFLOATS:%d", m_truncateBits);
	}
}

void Crinkler::initProgressBar() {
	m_progressBar.addProgressBar(&m_consoleBar);
	if(m_showProgressBar)
		m_progressBar.addProgressBar(&m_windowBar);
	m_progressBar.init();
}

void Crinkler::deinitProgressBar() {
	m_progressBar.deinit();
}