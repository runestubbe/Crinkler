#include "Crinkler.h"
#include "Compressor/Compressor.h"
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
	m_truncateBits(64),
	m_overrideAlignments(false),
	m_unalignCode(false),
	m_alignmentBits(0),
	m_runInitializers(1),
	m_largeAddressAware(0),
	m_saturate(0),
	m_stripExports(false)
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

	if(entry->value > 0) {
		Log::warning("", "Entry point not at start of section, jump necessary");
	}

	return entry;
}

void Crinkler::removeUnreferencedHunks(Hunk* base)
{
	//check dependencies and remove unused hunks
	list<Hunk*> startHunks;
	startHunks.push_back(base);

	//keep hold of exported symbols
	for (const Export& e : m_exports)  {
		if (e.hasValue()) {
			Symbol* sym = m_hunkPool.findSymbol(e.getName().c_str());
			if (sym && !sym->fromLibrary) {
				Log::error("", "Cannot create integer symbol '%s' for export: symbol already exists.", e.getName().c_str());
			}
		} else {
			Symbol* sym = m_hunkPool.findSymbol(e.getSymbol().c_str());
			if (sym) {
				if (sym->hunk->getRawSize() == 0) {
					Log::error("", "Export of uninitialized symbol '%s' not supported.", e.getSymbol().c_str());
				}
				startHunks.push_back(sym->hunk);
			} else {
				Log::error("", "Cannot find symbol '%s' to be exported under name '%s'.", e.getSymbol().c_str(), e.getName().c_str());
			}
		}
	}

	//hack to ensure that LoadLibrary & MessageBox is there to be used in the import code
	Symbol* loadLibrary = m_hunkPool.findSymbol("__imp__LoadLibraryA@4"); 
	Symbol* messageBox = m_hunkPool.findSymbol("__imp__MessageBoxA@16");
	Symbol* dynamicInitializers = m_hunkPool.findSymbol("__DynamicInitializers");
	if(loadLibrary != NULL)
		startHunks.push_back(loadLibrary->hunk);
	if(m_useSafeImporting && messageBox != NULL)
		startHunks.push_back(messageBox->hunk);
	if(dynamicInitializers != NULL)
		startHunks.push_back(dynamicInitializers->hunk);

	m_hunkPool.removeUnreferencedHunks(startHunks);
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

void Crinkler::loadImportCode(bool use1kMode, bool useSafeImporting, bool useDllFallback, bool useRangeImport) {
	//do imports
	if (use1kMode){
		load(import1KObj, import1KObj_end - import1KObj, "Crinkler import");
	} else {
		if (useSafeImporting)
			if (useDllFallback)
				if (useRangeImport)
					load(importSafeFallbackRangeObj, importSafeFallbackRangeObj_end - importSafeFallbackRangeObj, "Crinkler import");
				else
					load(importSafeFallbackObj, importSafeFallbackObj_end - importSafeFallbackObj, "Crinkler import");
			else
				if (useRangeImport)
					load(importSafeRangeObj, importSafeRangeObj_end - importSafeRangeObj, "Crinkler import");
				else
					load(importSafeObj, importSafeObj_end - importSafeObj, "Crinkler import");
		else
			if (useDllFallback)
				Log::error("", "DLL fallback cannot be used with unsafe importing");
			else
				if (useRangeImport)
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
	models = new Hunk("models", 0, 0, 0, modelsSize, modelsSize);
	models->addSymbol(new Symbol("_Models", 0, SYMBOL_IS_RELOCATEABLE, models));
	char* ptr = models->getPtr();
	*(unsigned int*)ptr = -(CRINKLER_CODEBASE+splittingPoint);		ptr += sizeof(unsigned int);
	*(unsigned int*)ptr = w1;										ptr += sizeof(unsigned int);
	for(int m = 0; m < m_modellist1.nmodels; m++)
		*ptr++ = masks1[m];
	*(unsigned int*)ptr = -(CRINKLER_CODEBASE+rawsize);				ptr += sizeof(unsigned int);
	*(unsigned int*)ptr = w2;										ptr += sizeof(unsigned int);
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
		CompressionStream cs(buff, NULL, maxsize, m_saturate);
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

int Crinkler::estimateModels(unsigned char* data, int datasize, int splittingPoint, bool reestimate, bool use1kMode) {
	int idealsize;
	
	bool verbose = (m_printFlags & PRINT_MODELS) != 0;

	if (use1kMode)
	{
		m_progressBar.beginTask(reestimate ? "Reestimating models" : "Estimating models");
		m_modellist1k = ApproximateModels1k(data, datasize, &idealsize, &m_progressBar, verbose);
		m_progressBar.endTask();
	}
	else
	{
		int size1, size2;
		m_progressBar.beginTask(reestimate ? "Reestimating models for code" : "Estimating models for code");
		m_modellist1 = ApproximateModels4k(data, splittingPoint, CRINKLER_BASEPROB, m_saturate, &size1, &m_progressBar, verbose, m_compressionType);
		m_progressBar.endTask();

		m_progressBar.beginTask(reestimate ? "Reestimating models for data" : "Estimating models for data");
		m_modellist2 = ApproximateModels4k(data + splittingPoint, datasize - splittingPoint, CRINKLER_BASEPROB, m_saturate, &size2, &m_progressBar, verbose, m_compressionType);
		m_progressBar.endTask();

		idealsize = size1 + size2;
	}
	
	float bytesize = idealsize / (float) (BITPREC * 8);
	printf(reestimate ? "\nReestimated ideal compressed total size: %.2f\n" :
						"\nEstimated ideal compressed total size: %.2f\n", 
						bytesize);
	return idealsize;
}

void Crinkler::setHeaderSaturation(Hunk* header) {
	if (m_saturate) {
		static const unsigned char saturateCode[] = { 0x75, 0x03, 0xFE, 0x0C, 0x1F };
		header->insert(header->findSymbol("_SaturatePtr")->value, saturateCode, sizeof(saturateCode));
		*(header->getPtr() + header->findSymbol("_SaturateAdjust1Ptr")->value) += sizeof(saturateCode);
		*(header->getPtr() + header->findSymbol("_SaturateAdjust2Ptr")->value) -= sizeof(saturateCode);
	}
}

void Crinkler::setHeaderConstants(Hunk* header, Hunk* phase1, int hashsize, int boostfactor, int baseprob0, int baseprob1, unsigned int modelmask, int subsystem_version, int exports_rva, bool use1kHeader)
{
	header->addSymbol(new Symbol("_HashTableSize", hashsize/2, 0, header));
	header->addSymbol(new Symbol("_UnpackedData", CRINKLER_CODEBASE, 0, header));
	header->addSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, header));
	header->addSymbol(new Symbol("_ModelMask", modelmask << 1, 0, header));

	if (use1kHeader)
	{
		int virtualSize = align(phase1->getVirtualSize() + 65536 * 2, 24);	//TODO: does this make sense?
		
		*(header->getPtr() + header->findSymbol("_BaseProbPtr0")->value) = baseprob0;
		*(header->getPtr() + header->findSymbol("_BaseProbPtr1")->value) = baseprob1;
		*(header->getPtr() + header->findSymbol("_BoostFactorPtr")->value) = boostfactor;
		*(unsigned short*)(header->getPtr() + header->findSymbol("_DepackEndPositionPtr")->value) = phase1->getRawSize() + CRINKLER_CODEBASE;
		*(header->getPtr() + header->findSymbol("_VirtualSizeHighBytePtr")->value) = virtualSize >> 24;
	}
	else
	{
		int virtualSize = align(max(phase1->getVirtualSize(), phase1->getRawSize() + hashsize), 16);
		header->addSymbol(new Symbol("_VirtualSize", virtualSize, 0, header));
		*(header->getPtr() + header->findSymbol("_BaseProbPtr")->value) = CRINKLER_BASEPROB;
		*(header->getPtr() + header->findSymbol("_ModelSkipPtr")->value) = m_modellist1.nmodels + 8;
		if (exports_rva) {
			*(int*)(header->getPtr() + header->findSymbol("_ExportTableRVAPtr")->value) = exports_rva;
			*(int*)(header->getPtr() + header->findSymbol("_NumberOfDataDirectoriesPtr")->value) = 1;
		}
	}
	
	*(header->getPtr() + header->findSymbol("_SubsystemTypePtr")->value) = subsystem_version;
	*((short*)(header->getPtr() + header->findSymbol("_LinkerVersionPtr")->value)) = CRINKLER_LINKER_VERSION;

	if (phase1->getRawSize() >= 2 && (phase1->getPtr()[0] == 0x5F || phase1->getPtr()[2] == 0x5F))
	{
		// Code starts with POP EDI => call transform
		*(header->getPtr() + header->findSymbol("_SpareNopPtr")->value) = 0x57; // PUSH EDI
	}
	if (m_largeAddressAware)
	{
		*((short*)(header->getPtr() + header->findSymbol("_CharacteristicsPtr")->value)) |= 0x0020;
	}
}

void Crinkler::recompress(const char* input_filename, const char* output_filename) {
	MemoryFile file(input_filename);
	unsigned char* indata = (unsigned char*)file.getPtr();
	if (indata == NULL) {
		Log::error("", "Cannot open file '%s'\n", input_filename);
	}

	FILE* outfile = 0;
	if (strcmp(input_filename, output_filename) != 0) {
		//open output file now, just to be sure :)
		if(fopen_s(&outfile, output_filename, "wb")) {
			Log::error("", "Cannot open '%s' for writing", output_filename);
			return;
		}
	}

	int length = file.getSize();
	if(length < 200)
	{
		notCrinklerFileError();
	}

	unsigned int pe_header_offset = *(unsigned int*)&indata[0x3C];

	bool is_compatibility_header = false;
	char majorlv = 0, minorlv = 0;

	if(pe_header_offset == 4)
	{
		is_compatibility_header = false;
		majorlv = indata[2];
		minorlv = indata[3];
	}
	else if(pe_header_offset == 12)
	{
		is_compatibility_header = true;
		majorlv = indata[38];
		minorlv = indata[39];
	}
	else
	{
		notCrinklerFileError();
	}
	
	if (majorlv < '0' || majorlv > '9' ||
		minorlv < '0' || minorlv > '9') {
			notCrinklerFileError();
	}

	//oops: 0.6 -> 1.0
	if (majorlv == '0' && minorlv == '6') {
		majorlv = '1';
		minorlv = '0';
	}
	int version = (majorlv-'0')*10 + (minorlv-'0');

	if (is_compatibility_header && version >= 14) {
		printf("File compressed using a pre-1.4 Crinkler and recompressed using Crinkler version %c.%c\n", majorlv, minorlv);
	} else {
		printf("File compressed or recompressed using Crinkler version %c.%c\n", majorlv, minorlv);
	}

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


	int virtualSize = (*(int*)&indata[pe_header_offset+0x50]) - 0x20000;
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
		if(version < 13)
		{
			if(indata[i] == 0x4B && indata[i+1] == 0x61 && indata[i+2] == 0x7F) {
				depacker_start = i;
			}
		}
		else if(version == 13)
		{
			if(indata[i] == 0x0F && indata[i+1] == 0xA3 && indata[i+2] == 0x2D) {
				depacker_start = i;
			}
		}
		else
		{
			if(indata[i] == 0xE8 && indata[i+5] == 0x60 && indata[i+6] == 0xAD) {
				depacker_start = i;
			}
		}
		
		if(indata[i] == 0xBE && indata[i+3] == 0x40 && indata[i+4] == 0x00) {
			models_address = *(int*)&indata[i+1];
		}
	}

	if(hashtable_size == -1 || return_offset == -1 || (depacker_start == -1 && is_compatibility_header) || models_address == -1)
	{
		notCrinklerFileError();
	}

	int models_offset = models_address-CRINKLER_IMAGEBASE;
	unsigned int weightmask1 = *(unsigned int*)&indata[models_offset+4];
	unsigned char* models1 = &indata[models_offset+8];
	m_modellist1.setFromModelsAndMask(models1, weightmask1);
	int modelskip = 8 + m_modellist1.nmodels;
	unsigned int weightmask2 = *(unsigned int*)&indata[models_offset+modelskip+4];
	unsigned char* models2 = &indata[models_offset+modelskip+8];
	m_modellist2.setFromModelsAndMask(models2, weightmask2);

	CompressionType compmode = m_modellist1.detectCompressionType();
	int subsystem_version = indata[pe_header_offset+0x5C];
	int large_address_aware = (*(unsigned short *)&indata[pe_header_offset+0x16] & 0x0020) != 0;

	static const unsigned char saturateCode[] = { 0x75, 0x03, 0xFE, 0x0C, 0x1F };
	bool saturate = std::search(indata, indata + length, std::begin(saturateCode), std::end(saturateCode)) != indata + length;
	if (m_saturate == -1) m_saturate = saturate;

	int exports_rva = *(int*)&indata[pe_header_offset + 0x78];

	printf("Original file size: %d\n", length);
	printf("Original Virtual size: %d\n", virtualSize);
	printf("Original Subsystem type: %s\n", subsystem_version == 3 ? "CONSOLE" : "WINDOWS");
	printf("Original Large address aware: %s\n", large_address_aware ? "YES" : "NO");
	printf("Original Compression mode: %s\n", compmode == COMPRESSION_INSTANT ? "INSTANT" : "FAST/SLOW");
	printf("Original Saturate counters: %s\n", saturate ? "YES" : "NO");
	printf("Original Hash size: %d\n", hashtable_size);

	int rawsize;
	int splittingPoint;

	if(version >= 13) {
		rawsize = -(*(int*)&indata[models_offset+modelskip])-CRINKLER_CODEBASE;
		splittingPoint = -(*(int*)&indata[models_offset])-CRINKLER_CODEBASE;
	} else {
		rawsize = (*(int*)&indata[models_offset+modelskip]) / 8;
		splittingPoint = (*(int*)&indata[models_offset]) / 8;
	}

	printf("Code size: %d\n", splittingPoint);
	printf("Data size: %d\n", rawsize-splittingPoint);
	printf("\n");

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

	//patch calltrans code
	int import_offset = 0;
	if (rawdata[0] == 0x89 && rawdata[1] == 0xD7) { // MOV EDI, EDX
		// Old calltrans code - convert to new
		unsigned int ncalls = rawdata[5];
		rawdata[0] = 0x5F; // POP EDI
		rawdata[1] = 0xB9; // MOV ECX, DWORD
		*((unsigned int *)&rawdata[2]) = ncalls;
		printf("Call transformation code successfully patched.\n");
		import_offset = 24;
	} else if (rawdata[0] == 0x5F) { // POP EDI
		// New calltrans code
		printf("Call transformation code does not need patching.\n");
		import_offset = 24;
	}

	//patch import code
	static const unsigned char old_import_code[] = {0x31, 0xC0, 0x64, 0x8B, 0x40, 0x30, 0x8B, 0x40, 
													0x0C, 0x8B, 0x40, 0x1C, 0x8B, 0x40, 0x00, 0x8B,
													0x68, 0x08};
	static const unsigned char new_import_code[] = {0x64, 0x67, 0x8B, 0x47, 0x30, 0x8B, 0x40, 0x0C,
													0x8B, 0x40, 0x0C, 0x8B, 0x00, 0x8B, 0x00, 0x8B,
													0x68, 0x18};
	static const unsigned char new_import_code2[] ={0x58, 0x8B, 0x40, 0x0C, 0x8B, 0x40, 0x0C, 0x8B,
													0x00, 0x8B, 0x00, 0x8B, 0x68, 0x18};
	bool found_import = false;
	int hashes_address = -1;
	int hashes_address_offset = -1;
	int dll_names_address = -1;
	for (int i = import_offset ; i < splittingPoint-(int)sizeof(old_import_code) ; i++) {
		if (rawdata[i] == 0xBB) {
			hashes_address_offset = i + 1;
			hashes_address = *(int*)&rawdata[hashes_address_offset];
		}
		if (rawdata[i] == 0xBE) {
			dll_names_address = *(int*)&rawdata[i + 1];
		}
		if (memcmp(rawdata+i, old_import_code, sizeof(old_import_code)) == 0) {			//no calltrans
			memcpy(rawdata+i, new_import_code, sizeof(new_import_code));
			printf("Import code successfully patched.\n");
			found_import = true;
			break;
		}
		if (memcmp(rawdata+i, new_import_code, sizeof(new_import_code)) == 0 || memcmp(rawdata+i, new_import_code2, sizeof(new_import_code2)) == 0)
		{
			printf("Import code does not need patching.\n");
			found_import = true;
			break;
		}
	}
	if (!found_import) {
		Log::error("", "Cannot find old import code to patch\n");
	}

	printf("\n");

	if (!m_replaceDlls.empty()) {
		char* name = (char*)&rawdata[dll_names_address + 1 - CRINKLER_CODEBASE];
		while (name[0] != (char)0xFF) {
			if (m_replaceDlls.count(name)) {
				assert(m_replaceDlls[name].length() == strlen(name));
				strcpy(name, m_replaceDlls[name].c_str());
			}
			name += strlen(name) + 2;
		}
	}

	HunkList* headerHunks = NULL;
	if (is_compatibility_header)
	{
		headerHunks = m_hunkLoader.load(headerCompatibilityObj, headerCompatibilityObj_end - headerCompatibilityObj, "crinkler header");
	}
	else
	{
		headerHunks = m_hunkLoader.load(headerObj, headerObj_end - headerObj, "crinkler header");
	}

	Hunk* header = headerHunks->findSymbol("_header")->hunk;
	Hunk* depacker = headerHunks->findSymbol("_DepackEntry")->hunk;
	setHeaderSaturation(depacker);

	int new_hashes_address = is_compatibility_header ? CRINKLER_IMAGEBASE : CRINKLER_IMAGEBASE + header->getRawSize();
	*(int*)&rawdata[hashes_address_offset] = new_hashes_address;

	Hunk* phase1 = new Hunk("linked", (char*)rawdata, HUNK_IS_CODE|HUNK_IS_WRITEABLE, 0, rawsize, virtualSize);
	delete[] rawdata;

	// Handle exports
	std::set<Export> exports;
	printf("Original Exports:");
	if (exports_rva) {
		exports = stripExports(phase1, exports_rva);
		printf("\n");
		printExports(exports);
		if (!m_stripExports) {
			for (const Export& e : exports) {
				addExport(e);
			}
		}
	} else {
		printf(" NONE\n");
	}
	printf("Resulting Exports:");
	if (!m_exports.empty()) {
		printf("\n");
		printExports(m_exports);
		for (const Export& e : m_exports) {
			if (!e.hasValue()) {
				Symbol *sym = phase1->findSymbol(e.getSymbol().c_str());
				if (!sym) {
					Log::error("", "Cannot find symbol '%s' to be exported under name '%s'.", e.getSymbol().c_str(), e.getName().c_str());
				}
			}
		}

		phase1->setVirtualSize(phase1->getRawSize());
		Hunk* export_hunk = createExportTable(m_exports);
		HunkList hl;
		hl.addHunkBack(phase1);
		hl.addHunkBack(export_hunk);
		Hunk* with_exports = hl.toHunk("linked", CRINKLER_CODEBASE);
		hl.clear();
		with_exports->setVirtualSize(virtualSize);
		with_exports->relocate(CRINKLER_CODEBASE);
		delete phase1;
		phase1 = with_exports;
	} else {
		printf(" NONE\n");
	}
	phase1->trim();

	printf("\nRecompressing...\n");

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
			idealsize = estimateModels((unsigned char*)phase1->getPtr(), phase1->getRawSize(), splittingPoint, false, false);
			
			//hashing time
			best_hashsize = optimizeHashsize((unsigned char*)phase1->getPtr(), phase1->getRawSize(), best_hashsize, splittingPoint, m_hashtries);
			deinitProgressBar();
		}
	}

	CompressionStream cs(data, sizefill, maxsize, m_saturate);
	cs.Compress((unsigned char*)phase1->getPtr(), splittingPoint, m_modellist1, baseprob, best_hashsize, true, false);
	cs.Compress((unsigned char*)phase1->getPtr() + splittingPoint, phase1->getRawSize() - splittingPoint, m_modellist2, baseprob, best_hashsize, false, true);
	size = cs.Close();
	if(m_compressionType != -1 && m_compressionType != COMPRESSION_INSTANT) {
		float byteslost = size - idealsize / (float) (BITPREC * 8);
		printf("Real compressed total size: %d\nBytes lost to hashing: %.2f\n", size, byteslost);
	}

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

	if(is_compatibility_header)
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
	if(is_compatibility_header)
	{
		phase2list.addHunkBack(depacker);
	}
	else
	{
		//create hashes hunk
		int hashes_offset = hashes_address - CRINKLER_IMAGEBASE;
		int hashes_bytes = models_offset - hashes_offset;
		Hunk* hashHunk = new Hunk("HashHunk", (char*)&indata[hashes_offset], 0, 0, hashes_bytes, hashes_bytes);
		phase2list.addHunkBack(hashHunk);
	}
	phase2list.addHunkBack(models);
	phase2list.addHunkBack(phase1Compressed);
	header->addSymbol(new Symbol("_HashTable", CRINKLER_SECTIONSIZE*2+phase1->getRawSize(), SYMBOL_IS_RELOCATEABLE, header));

	Hunk* phase2 = phase2list.toHunk("final", CRINKLER_IMAGEBASE);
	if(m_subsystem >= 0) {
		subsystem_version = (m_subsystem == SUBSYSTEM_WINDOWS) ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI;
	}
	if (m_largeAddressAware == -1) {
		m_largeAddressAware = large_address_aware;
	}
	int new_exports_rva = m_exports.empty() ? 0 : phase1->findSymbol("_ExportTable")->value + CRINKLER_CODEBASE - CRINKLER_IMAGEBASE;
	setHeaderConstants(phase2, phase1, best_hashsize, 0, 0, 0, 0, subsystem_version, new_exports_rva, false);
	phase2->relocate(CRINKLER_IMAGEBASE);

	if (!outfile) {
		if(fopen_s(&outfile, output_filename, "wb")) {
			Log::error("", "Cannot open '%s' for writing", output_filename);
			return;
		}
	}
	fwrite(phase2->getPtr(), 1, phase2->getRawSize(), outfile);
	fclose(outfile);

	printf("\nOutput file: %s\n", output_filename);
	printf("Final file size: %d\n\n", phase2->getRawSize());

	delete phase1;
	delete phase2;
}

Hunk* Crinkler::createDynamicInitializerHunk()
{
	const int num_hunks = m_hunkPool.getNumHunks();
	std::vector<Symbol*> symbols;
	for(int i = 0; i < num_hunks; i++)
	{
		Hunk* hunk = m_hunkPool[i];
		if(endsWith(hunk->getName(), "CRT$XCU"))
		{
			int num_relocations = hunk->getNumRelocations();
			relocation* relocations = hunk->getRelocations();
			for(int i = 0; i < num_relocations; i++)
			{
				symbols.push_back(m_hunkPool.findSymbol(relocations[i].symbolname.c_str()));
			}
		}
	}

	if(!symbols.empty())
	{
		const int num_symbols = symbols.size();
		const int hunk_size = num_symbols*5;
		Hunk* hunk = new Hunk("dynamic initializer calls", NULL, HUNK_IS_CODE, 0, hunk_size, hunk_size);

		char* ptr = hunk->getPtr();
		for(int i = 0; i < num_symbols; i++)
		{
			*ptr++ = (char)0xE8;
			*ptr++ = 0x00;
			*ptr++ = 0x00;
			*ptr++ = 0x00;
			*ptr++ = 0x00;
			
			relocation r;
			r.offset = i*5+1;
			r.symbolname = symbols[i]->name;
			r.type = RELOCTYPE_REL32;
			hunk->addRelocation(r);
		}
		hunk->addSymbol(new Symbol("__DynamicInitializers", 0, SYMBOL_IS_RELOCATEABLE, hunk));
		printf("\nIncluded %d dynamic initializer%s.\n", num_symbols, num_symbols == 1 ? "" : "s");
		return hunk;
	}
	return NULL;
}

void Crinkler::link(const char* filename) {
	//open output file now, just to be sure :)
	FILE* outfile;
	int old_filesize = 0;
	if (!fopen_s(&outfile, filename, "rb")) {
		//find old size
		fseek(outfile, 0, SEEK_END);
		old_filesize = ftell(outfile);
		fclose(outfile);
	}
	if(fopen_s(&outfile, filename, "wb")) {
		Log::error("", "Cannot open '%s' for writing", filename);
		return;
	}


	//find entry hunk and move it to front
	Symbol* entry = findEntryPoint();
	if(entry == NULL)
		return;

	Hunk* dynamicInitializersHunk = NULL;
	if (m_runInitializers) {
		dynamicInitializersHunk = createDynamicInitializerHunk();
		if(dynamicInitializersHunk)
		{
			m_hunkPool.addHunkBack(dynamicInitializersHunk);
		}
	}

	//color hunks from entry hunk
	removeUnreferencedHunks(entry->hunk);

	//replace dlls
	replaceDlls(m_hunkPool);

	if (m_overrideAlignments) overrideAlignments(m_hunkPool);

	//1byte align entry point and other sections
	int n_unaligned = 0;
	bool entry_point_unaligned = false;
	if(entry->hunk->getAlignmentBits() > 0) {
		entry->hunk->setAlignmentBits(0);
		n_unaligned++;
		entry_point_unaligned = true;
	}
	if (m_unalignCode) {
		for (int i = 0; i < m_hunkPool.getNumHunks(); i++) {
			Hunk* hunk = m_hunkPool[i];
			if (hunk->getFlags() & HUNK_IS_CODE && !(hunk->getFlags() & HUNK_IS_ALIGNED) && hunk->getAlignmentBits() > 0) {
				hunk->setAlignmentBits(0);
				n_unaligned++;
			}
		}
	}
	if (n_unaligned > 0) {
		printf("Forced alignment of %d code hunk%s to 1", n_unaligned, n_unaligned > 1 ? "s" : "");
		if (entry_point_unaligned) {
			printf(" (including entry point)");
		}
		printf(".\n");
	}

	//load appropriate header
	HunkList* headerHunks = m_1KMode ?	m_hunkLoader.load(header1KObj, header1KObj_end - header1KObj, "crinkler header") :
										m_hunkLoader.load(headerObj, headerObj_end - headerObj, "crinkler header");

	Hunk* header = headerHunks->findSymbol("_header")->hunk;
	if(!m_1KMode)
		setHeaderSaturation(header);
	Hunk* hashHunk = NULL;

	bool useTinyImport = m_1KMode;

	int hash_bits;
	int max_dll_name_length;
	bool usesRangeImport=false;
	{	//add imports
		HunkList* importHunkList = useTinyImport ?	ImportHandler::createImportHunks1K(&m_hunkPool, (m_printFlags & PRINT_IMPORTS) != 0, hash_bits, max_dll_name_length) :
													ImportHandler::createImportHunks(&m_hunkPool, hashHunk, m_fallbackDlls, m_rangeDlls, (m_printFlags & PRINT_IMPORTS) != 0, usesRangeImport);
		m_hunkPool.removeImportHunks();
		m_hunkPool.append(importHunkList);
		delete importHunkList;
	}

	loadImportCode(useTinyImport, m_useSafeImporting, !m_fallbackDlls.empty(), usesRangeImport);

	Symbol* importSymbol = m_hunkPool.findSymbol("_Import");

	if(dynamicInitializersHunk)
	{		
		m_hunkPool.removeHunk(dynamicInitializersHunk);
		m_hunkPool.addHunkFront(dynamicInitializersHunk);
		dynamicInitializersHunk->setContinuation(entry);
	}

	Hunk* importHunk = importSymbol->hunk;

	m_hunkPool.removeHunk(importHunk);
	m_hunkPool.addHunkFront(importHunk);
	importHunk->setAlignmentBits(0);
	importHunk->setContinuation(dynamicInitializersHunk ? dynamicInitializersHunk->findSymbol("__DynamicInitializers") : entry);
	
	// Make sure import and startup code has access to the _ImageBase address
	importHunk->addSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, importHunk));
	importHunk->addSymbol(new Symbol("___ImageBase", CRINKLER_IMAGEBASE, 0, importHunk));


	if (useTinyImport)
	{
		*(importHunk->getPtr() + importHunk->findSymbol("_HashShiftPtr")->value) = 32 - hash_bits;
		*(importHunk->getPtr() + importHunk->findSymbol("_MaxNameLengthPtr")->value) = max_dll_name_length;
	}


	//truncate floats
	if(m_truncateFloats) {
		printf("\nTruncating floats:\n");
		m_hunkPool.roundFloats(m_truncateBits);
	}

	if (!m_exports.empty()) {
		m_hunkPool.addHunkBack(createExportTable(m_exports));
	}

	//sort hunks heuristically
	HeuristicHunkSorter::sortHunkList(&m_hunkPool);

	//create phase 1 data hunk
	int splittingPoint;
	Hunk* phase1, *phase1Untransformed;
	m_hunkPool[0]->addSymbol(new Symbol("_HeaderHashes", CRINKLER_IMAGEBASE+header->getRawSize(), SYMBOL_IS_SECTION, m_hunkPool[0]));

	if (!m_transform->linkAndTransform(&m_hunkPool, importSymbol, CRINKLER_CODEBASE, phase1, &phase1Untransformed, &splittingPoint, true, m_1KMode))
	{
		// Transform failed, run again
		delete phase1;
		delete phase1Untransformed;
		m_transform->linkAndTransform(&m_hunkPool, importSymbol, CRINKLER_CODEBASE, phase1, &phase1Untransformed, &splittingPoint, false, m_1KMode);
	}
	int maxsize = phase1->getRawSize()*2+1000;	//allocate plenty of memory	

	printf("\nUncompressed size of code: %5d\n", splittingPoint);
	printf("Uncompressed size of data: %5d\n", phase1->getRawSize() - splittingPoint);

	int* sizefill = new int[maxsize];

	unsigned char* data = new unsigned char[maxsize];
	int best_hashsize = previousPrime(m_hashsize/2)*2;
	Hunk* phase1Compressed;
	int size, idealsize = 0;
	if (m_1KMode || m_compressionType != COMPRESSION_INSTANT)
	{
		initProgressBar();
		idealsize = estimateModels((unsigned char*)phase1->getPtr(), phase1->getRawSize(), splittingPoint, false, m_1KMode);

		if(m_hunktries > 0)
		{
			EmpiricalHunkSorter::sortHunkList(&m_hunkPool, *m_transform, m_modellist1, m_modellist2, m_modellist1k, CRINKLER_BASEPROB, m_saturate, m_hunktries, m_showProgressBar ? &m_windowBar : NULL, m_1KMode);
			delete phase1;
			delete phase1Untransformed;
			m_transform->linkAndTransform(&m_hunkPool, importSymbol, CRINKLER_CODEBASE, phase1, &phase1Untransformed, &splittingPoint, true, m_1KMode);

			idealsize = estimateModels((unsigned char*)phase1->getPtr(), phase1->getRawSize(), splittingPoint, true, m_1KMode);
		}
		
		//hashing time
		if (!m_1KMode)
		{
			best_hashsize = optimizeHashsize((unsigned char*)phase1->getPtr(), phase1->getRawSize(), best_hashsize, splittingPoint, m_hashtries);
		}
		
		deinitProgressBar();
	}

	Hunk* modelHunk = nullptr;

	if (m_1KMode)
	{
		size = Compress1K((unsigned char*)phase1->getPtr(), phase1->getRawSize(), data, maxsize, m_modellist1k.boost, m_modellist1k.baseprob0, m_modellist1k.baseprob1, m_modellist1k.modelmask, sizefill, nullptr);
	}
	else
	{
		CompressionStream cs(data, sizefill, maxsize, m_saturate);
		cs.Compress((unsigned char*)phase1->getPtr(), splittingPoint, m_modellist1, CRINKLER_BASEPROB, best_hashsize, true, false);
		cs.Compress((unsigned char*)phase1->getPtr() + splittingPoint, phase1->getRawSize() - splittingPoint, m_modellist2, CRINKLER_BASEPROB, best_hashsize, false, true);
		size = cs.Close();

		modelHunk = createModelHunk(splittingPoint, phase1->getRawSize());
	}
	
	if(!m_1KMode && m_compressionType != COMPRESSION_INSTANT) {
		float byteslost = size - idealsize / (float) (BITPREC * 8);
		printf("Real compressed total size: %d\nBytes lost to hashing: %.2f\n", size, byteslost);
	}

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

	HunkList phase2list;
	phase2list.addHunkBack(header);
	if (hashHunk)
	{
		phase2list.addHunkBack(hashHunk);
	}
	if (modelHunk)
	{
		phase2list.addHunkBack(modelHunk);
	}
	
	phase2list.addHunkBack(phase1Compressed);
	if (!m_1KMode)
	{
		header->addSymbol(new Symbol("_HashTable", CRINKLER_SECTIONSIZE * 2 + phase1->getRawSize(), SYMBOL_IS_RELOCATEABLE, header));
	}
	
	Hunk* phase2 = phase2list.toHunk("final", CRINKLER_IMAGEBASE);
	//add constants
	int exports_rva = m_exports.empty() ? 0 : phase1->findSymbol("_ExportTable")->value + CRINKLER_CODEBASE - CRINKLER_IMAGEBASE;
	setHeaderConstants(phase2, phase1, best_hashsize, m_modellist1k.boost, m_modellist1k.baseprob0, m_modellist1k.baseprob1, m_modellist1k.modelmask, m_subsystem == SUBSYSTEM_WINDOWS ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI, exports_rva, m_1KMode);
	phase2->relocate(CRINKLER_IMAGEBASE);

	fwrite(phase2->getPtr(), 1, phase2->getRawSize(), outfile);
	fclose(outfile);

	printf("\nOutput file: %s\n", filename);
	printf("Final file size: %d", phase2->getRawSize());
	if (old_filesize)
	{
		if (old_filesize != phase2->getRawSize()) {
			printf(" (previous size %d)", old_filesize);
		} else {
			printf(" (no change)");
		}
	}
	printf("\n\n");

	if (phase2->getRawSize() > 128*1024)
	{
		Log::error(filename, "Output file too big. Crinkler does not support final file sizes of more than 128k.");
	}

	delete phase1;
	delete phase1Untransformed;
	delete phase2;
}

void Crinkler::printOptions(FILE *out) {
	fprintf(out, " /SUBSYSTEM:%s", m_subsystem == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
	if (m_largeAddressAware) {
		fprintf(out, " /LARGEADDRESSAWARE");
	}
	if (!m_entry.empty()) {
		fprintf(out, " /ENTRY:%s", m_entry.c_str());
	}
	fprintf(out, " /COMPMODE:%s", compTypeName(m_compressionType));
	fprintf(out, " /HASHSIZE:%d", m_hashsize/1048576);
	if (m_compressionType != COMPRESSION_INSTANT) {
		fprintf(out, " /HASHTRIES:%d", m_hashtries);
		fprintf(out, " /ORDERTRIES:%d", m_hunktries);
	}
	for(int i = 0; i < (int)m_rangeDlls.size(); i++) {
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
	if (m_overrideAlignments) {
		fprintf(out, " /OVERRIDEALIGNMENTS");
		if (m_alignmentBits != -1) {
			fprintf(out, ":%d", m_alignmentBits);
		}
	}
	if (!m_runInitializers) {
		fprintf(out, " /NOINITIALIZERS");
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