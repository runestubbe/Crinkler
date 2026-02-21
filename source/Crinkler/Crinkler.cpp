#define NOMINMAX
#include "Crinkler.h"
#include "../Compressor/Compressor.h"
#include "Fix.h"

#include <set>
#include <ctime>
#include <ppl.h>

#include "PartList.h"
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

#include <memory>
#include <format>
#include <iterator>

using namespace std;

static int PreviousPrime(int n) {
in:
	n = (n - 2) | 1;
	for (int i = 3; i * i < n; i += 2) {
		if (n / i * i == n) goto in;
	}
	return n;
}

static void VerboseLabels(CompressionReportRecord* csr) {
	if(csr->type & RECORD_ROOT) {
		printf("\nlabel name                                   pos comp-pos      size compsize");
	} else {
		string strippedName = StripCrinklerSymbolPrefix(csr->name.c_str());
		if(csr->type & RECORD_PART)
			printf("\n%-38.38s", strippedName.c_str());
		else if(csr->type & RECORD_SECTION)
			printf("  %-36.36s", strippedName.c_str());
		else if(csr->type & RECORD_PUBLIC)
			printf("    %-34.34s", strippedName.c_str());
		else
			printf("      %-32.32s", strippedName.c_str());

		if(csr->compressedPos >= 0)
			printf(" %9d %8.2f %9d %8.2f\n", csr->pos, csr->compressedPos / (BIT_PRECISION *8.0f), csr->size, csr->compressedSize / (BIT_PRECISION *8.0f));
		else
			printf(" %9d          %9d\n", csr->pos, csr->size);
	}

	for(CompressionReportRecord* record : csr->children)
		VerboseLabels(record);
}

static void ProgressUpdateCallback(void* userData, int n, int max)
{
	ProgressBar* progressBar = (ProgressBar*)userData;
	progressBar->Update(n, max);
}

static void NotCrinklerFileError() {
	Log::Error("", "Input file is not a Crinkler compressed executable");
}

Crinkler::Crinkler():
	m_hunkList("HunkList", true),
	m_subsystem(SUBSYSTEM_WINDOWS),
	m_hashsize(100*1024*1024),
	m_compressionType(COMPRESSION_FAST),
	m_reuseType(REUSE_OFF),
	m_useSafeImporting(true),
	m_hashtries(0),
	m_hunktries(0),
	m_printFlags(0),
	m_showProgressBar(false),
	m_useTinyHeader(false),
	m_useTinyImport(false),
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
	InitCompressor();
}


Crinkler::~Crinkler() {
}

void Crinkler::ReplaceDlls(Part& part) {
	set<string> usedDlls;
	// Replace DLL
	part.ForEachHunk([this, &usedDlls](Hunk* hunk)
		{
			if(hunk->GetFlags() & HUNK_IS_IMPORT) {
				map<string, string>::iterator it = m_replaceDlls.find(ToLower(hunk->GetImportDll()));
				if(it != m_replaceDlls.end()) {
					hunk->SetImportDll(it->second.c_str());
					usedDlls.insert(it->first);
				}
			}
		});
	
	// Warn about unused replace DLLs
	for(const auto& p : m_replaceDlls) {
		if(usedDlls.find(p.first) == usedDlls.end()) {
			Log::Warning("", "No functions were imported from replaced dll '%s'", p.first.c_str());
		}
	}
}


void Crinkler::OverrideAlignments(Part& part) {
	part.ForEachHunk([this](Hunk* hunk) {
		hunk->OverrideAlignment(m_alignmentBits);
		});
}

void Crinkler::Load(const char* filename) {
	bool result = m_hunkLoader.LoadFromFile(m_hunkList, filename);
	if(!result) {
		Log::Error(filename, "Unsupported file type");
	}
}

void Crinkler::Load(const char* data, int size, const char* module) {
	m_hunkLoader.Load(m_hunkList, data, size, module);
}

void Crinkler::AddRuntimeLibrary() {
	// Add minimal console entry point
	m_hunkLoader.Load(m_hunkList, runtimeObj, int(runtimeObj_end - runtimeObj), "runtime", false);

	// Add imports from msvcrt
	ForEachExportInDLL("msvcrt", [&](const char* name) {
		string symbolName = name[0] == '?' ? name : string("_") + name;
		string importName = string("__imp_" + symbolName);
		Hunk* importHunk = new Hunk(importName.c_str(), name, "msvcrt");
		Hunk* stubHunk = MakeCallStub(symbolName.c_str());
		importHunk->MarkHunkAsLibrary();
		stubHunk->MarkHunkAsLibrary();
		m_hunkList.AddHunkBack(importHunk);
		m_hunkList.AddHunkBack(stubHunk);
	});	
}

std::string Crinkler::GetEntrySymbolName() const {
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

Symbol*	Crinkler::FindEntryPoint() {
	// Place entry point in the beginning
	string entryName = GetEntrySymbolName();
	Symbol* entry = m_hunkList.FindUndecoratedSymbol(entryName.c_str());
	if(entry == NULL) {
		Log::Error("", "Cannot find entry point '%s'. See manual for details.", entryName.c_str());
		return NULL;
	}

	if(entry->value > 0) {
		Log::Warning("", "Entry point not at start of section, jump necessary");
	}

	return entry;
}

void Crinkler::RemoveUnreferencedHunks(Hunk* base)
{
	SymbolMap symbol_map(m_hunkList);

	// Check dependencies and remove unused hunks
	vector<Hunk*> startHunks;
	startHunks.push_back(base);

	// Keep hold of exported symbols
	for (const Export& e : m_exports)  {
		if (e.HasValue()) {
			Symbol* sym = symbol_map.FindSymbol(e.GetName());
			if (sym && !sym->fromLibrary) {
				Log::Error("", "Cannot create integer symbol '%s' for export: symbol already exists.", e.GetName().c_str());
			}
		} else {
			Symbol* sym = symbol_map.FindSymbol(e.GetSymbol().c_str());
			if (sym) {
				if (sym->hunk->GetRawSize() == 0) {
					sym->hunk->SetRawSize(sym->hunk->GetVirtualSize());
					Log::Warning("", "Uninitialized hunk '%s' forced to data section because of exported symbol '%s'.", sym->hunk->GetName(), e.GetSymbol().c_str());
				}
				startHunks.push_back(sym->hunk);
			} else {
				Log::Error("", "Cannot find symbol '%s' to be exported under name '%s'.", e.GetSymbol().c_str(), e.GetName().c_str());
			}
		}
	}

	// Hack to ensure that LoadLibrary & MessageBox is there to be used in the import code
	Symbol* loadLibrary = symbol_map.FindSymbol("__imp__LoadLibraryA@4");
	Symbol* messageBox = symbol_map.FindSymbol("__imp__MessageBoxA@16");
	Symbol* dynamicInitializers = symbol_map.FindSymbol("__DynamicInitializers");
	if(loadLibrary != NULL)
		startHunks.push_back(loadLibrary->hunk);
	if(m_useSafeImporting && !m_useTinyImport && messageBox != NULL)
		startHunks.push_back(messageBox->hunk);
	if(dynamicInitializers != NULL)
		startHunks.push_back(dynamicInitializers->hunk);

	m_hunkList.DeleteUnreferencedHunks(symbol_map, startHunks);
}

void Crinkler::LoadImportCode(bool use1kMode, bool useSafeImporting, bool useDllFallback, bool useRangeImport) {
	// Do imports
	if (use1kMode){
		Load(import1KObj, int(import1KObj_end - import1KObj), "Crinkler import");
	} else {
		if (useSafeImporting)
			if (useDllFallback)
				if (useRangeImport)
					Load(importSafeFallbackRangeObj, int(importSafeFallbackRangeObj_end - importSafeFallbackRangeObj), "Crinkler import");
				else
					Load(importSafeFallbackObj, int(importSafeFallbackObj_end - importSafeFallbackObj), "Crinkler import");
			else
				if (useRangeImport)
					Load(importSafeRangeObj, int(importSafeRangeObj_end - importSafeRangeObj), "Crinkler import");
				else
					Load(importSafeObj, int(importSafeObj_end - importSafeObj), "Crinkler import");
		else
			if (useDllFallback)
				Log::Error("", "DLL fallback cannot be used with unsafe importing");
			else
				if (useRangeImport)
					Load(importRangeObj, int(importRangeObj_end - importRangeObj), "Crinkler import");
				else
					Load(importObj, int(importObj_end - importObj), "Crinkler import");
	}
}

Hunk* Crinkler::CreateModelHunk4k(PartList& parts) {
	const int numInitializedParts = parts.GetNumInitializedParts();

	int modelsSize = 0;
	for (int i = 0; i < numInitializedParts; i++)
	{
		modelsSize += 6 + parts[i].m_model4k.nmodels;
	}
	
	Hunk* models = new Hunk("models", 0, 0, 0, modelsSize, modelsSize);
	models->AddSymbol(new Symbol("_Models", 0, SYMBOL_IS_RELOCATEABLE, models));

	unsigned char* ptr = (unsigned char*)models->GetPtr();
	for(int i = 0; i < numInitializedParts; i++)
	{
		Part& part = parts[i];
		const bool terminate = ((i + 1) == numInitializedParts);
		const unsigned int w = part.m_model4k.GetMaskList(ptr + 6, terminate);
		const int partEndPos = part.GetLinkedOffset() + part.GetLinkedSize();
		*(unsigned short*)ptr = -partEndPos;
		ptr += sizeof(unsigned short);
		*(unsigned int*)ptr = w;
		ptr += sizeof(unsigned int);
		ptr += part.m_model4k.nmodels;
	}

	return models;
}

int Crinkler::OptimizeHashsize(PartList& parts, Hunk* phase1, int hashsize, int tries) {
	if(tries == 0)
		return hashsize;

	int maxsize = phase1->GetRawSize()*2+1000;
	int bestsize = INT_MAX;
	int best_hashsize = hashsize;
	m_progressBar.BeginTask("Optimizing hash table size");

	const int numInitializedParts = parts.GetNumInitializedParts();

	unsigned int maxTinyHashSize = 0;
	unsigned char context[MAX_CONTEXT_LENGTH] = {};
	std::vector<HashBits> hashbits(numInitializedParts);
	for (int i = 0; i < numInitializedParts; i++)
	{
		Part& part = parts[i];
		bool first = (i == 0);
		bool last = ((i + 1) == numInitializedParts);
		hashbits[i] = ComputeHashBits(phase1->GetPtr() + part.GetLinkedOffset(), part.GetLinkedSize(), context, part.m_model4k, first, last);
		maxTinyHashSize = std::max(maxTinyHashSize, hashbits[i].tinyhashsize);
	}

	uint32_t* hashsizes = new uint32_t[tries];
	for (int i = 0; i < tries; i++) {
		hashsize = PreviousPrime(hashsize / 2) * 2;
		hashsizes[i] = hashsize;
	}

	int* sizes = new int[tries];

	int progress = 0;
	concurrency::combinable<vector<unsigned char>> buffers([maxsize]() { return vector<unsigned char>(maxsize, 0); });
	concurrency::combinable<vector<TinyHashEntry>> hashtabledata([&maxTinyHashSize]() { return vector<TinyHashEntry>(maxTinyHashSize); });
	concurrency::critical_section cs;

	concurrency::parallel_for(0, tries, [&](int i) {
		sizes[i] = CompressFromHashBits4k(hashbits.data(), hashtabledata.local().data(), numInitializedParts, buffers.local().data(), maxsize, m_saturate != 0, CRINKLER_BASEPROB, hashsizes[i], nullptr);

		Concurrency::critical_section::scoped_lock l(cs);
		m_progressBar.Update(++progress, m_hashtries);
	});

	for (int i = 0; i < tries; i++) {
		if (sizes[i] <= bestsize) {
			bestsize = sizes[i];
			best_hashsize = hashsizes[i];
		}
	}
	delete[] sizes;
	delete[] hashsizes;

	m_progressBar.EndTask();
	
	return best_hashsize;
}

int Crinkler::EstimateModels(PartList& parts, Hunk* phase1, bool reestimate, bool use1kMode)
{
	bool verbose = (m_printFlags & PRINT_MODELS) != 0;

	if (use1kMode)
	{
		assert(parts.GetNumInitializedParts() == 1);
		Part& part = parts[0];

		m_progressBar.BeginTask(reestimate ? "Reestimating models" : "Estimating models");
		
		int newCompressedSize;
		ModelList1k newModels = ApproximateModels1k(phase1->GetPtr(), phase1->GetRawSize(), &newCompressedSize, ProgressUpdateCallback, &m_progressBar);
		if(newCompressedSize < part.m_compressedSize)
		{
			part.m_compressedSize = newCompressedSize;
			part.m_model1k = newModels;
		}
		m_progressBar.EndTask();
		printf("\nEstimated compressed size: %.2f\n", part.m_compressedSize / (float)(BIT_PRECISION * 8));
		if(verbose) part.m_model1k.Print();
		return newCompressedSize;
	}
	else
	{
		const int numInitializedParts = parts.GetNumInitializedParts();
		for (int partIndex = 0; partIndex < numInitializedParts; partIndex++)
		{
			Part& part = parts[partIndex];

			unsigned char contexts[MAX_CONTEXT_LENGTH];
			for (int i = 0; i < MAX_CONTEXT_LENGTH; i++)
			{
				int srcpos = part.GetLinkedOffset() - MAX_CONTEXT_LENGTH + i;
				contexts[i] = srcpos >= 0 ? phase1->GetPtr()[srcpos] : 0;
			}

			int newCompressedSize;
			char desc[512];
			sprintf(desc, "%s models for %s", reestimate ? "Reestimating" : "Estimating", part.GetName());
			m_progressBar.BeginTask(desc);
			ModelList4k newModel = ApproximateModels4k(phase1->GetPtr() + part.GetLinkedOffset(), part.GetLinkedSize(), contexts, m_compressionType, m_saturate != 0, CRINKLER_BASEPROB, &newCompressedSize, ProgressUpdateCallback, &m_progressBar);
			m_progressBar.EndTask();

			if (newCompressedSize < part.m_compressedSize)
			{
				part.m_compressedSize = newCompressedSize;
				part.m_model4k = newModel;
			}

			if (verbose) {
				printf("Models: ");
				part.m_model4k.Print(stdout);
			}
			printf("Estimated compressed size of %s: %.2f\n", part.GetName(), part.m_compressedSize / (float)(BIT_PRECISION * 8));
		}

		return EvaluatePartsSize4k(parts, phase1);
	}
}

void Crinkler::SetHeaderSaturation(Hunk* header) {
	if (m_saturate) {
		static const unsigned char saturateCode[] = { 0x75, 0x03, 0xFE, 0x0C, 0x1F };
		header->Insert(header->FindSymbol("_SaturatePtr")->value, saturateCode, sizeof(saturateCode));
		*(header->GetPtr() + header->FindSymbol("_SaturateAdjust1Ptr")->value) += sizeof(saturateCode);
		*(header->GetPtr() + header->FindSymbol("_SaturateAdjust2Ptr")->value) -= sizeof(saturateCode);
	}
}

void Crinkler::SetHeaderConstantsCommon(Hunk* header, int subsystemVersion)
{
	header->AddSymbol(new Symbol("_UnpackedData", CRINKLER_CODEBASE, 0, header));
	header->AddSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, header));

	*(header->GetPtr() + header->FindSymbol("_SubsystemTypePtr")->value) = subsystemVersion;
	*((short*)(header->GetPtr() + header->FindSymbol("_LinkerVersionPtr")->value)) = CRINKLER_LINKER_VERSION;

	if (m_largeAddressAware)
	{
		*((short*)(header->GetPtr() + header->FindSymbol("_CharacteristicsPtr")->value)) |= 0x0020;
	}
}

void Crinkler::SetHeaderConstants1k(Hunk* header, Hunk* phase1, int boostfactor, int baseprob0, int baseprob1, unsigned int modelmask, int subsystemVersion)
{
	SetHeaderConstantsCommon(header, subsystemVersion);

	int virtualSizeHighByteOffset = header->FindSymbol("_VirtualSizeHighBytePtr")->value;
	int lowBytes = *(int*)(header->GetPtr() + virtualSizeHighByteOffset - 3) & 0xFFFFFF;
	int virtualSize = phase1->GetVirtualSize() + 65536 * 2;
		
	header->AddSymbol(new Symbol("_ModelMask", modelmask, 0, header));
	*(header->GetPtr() + header->FindSymbol("_BaseProbPtr0")->value) = baseprob0;
	*(header->GetPtr() + header->FindSymbol("_BaseProbPtr1")->value) = baseprob1;
	*(header->GetPtr() + header->FindSymbol("_BoostFactorPtr")->value) = boostfactor;
	*(unsigned short*)(header->GetPtr() + header->FindSymbol("_DepackEndPositionPtr")->value) = phase1->GetRawSize() + CRINKLER_CODEBASE;
	*(header->GetPtr() + virtualSizeHighByteOffset) = (virtualSize - lowBytes + 0xFFFFFF) >> 24;

}

void Crinkler::SetHeaderConstants4k(Hunk* header, Hunk* phase1, PartList& parts, int hashsize, int subsystemVersion, int exportsRVA)
{
	SetHeaderConstantsCommon(header, subsystemVersion);

	header->AddSymbol(new Symbol("_HashTableSize", hashsize / 2, 0, header));
	int virtualSize = Align(max(phase1->GetVirtualSize(), phase1->GetRawSize() + hashsize), 16);
	header->AddSymbol(new Symbol("_VirtualSize", virtualSize, 0, header));
	*(header->GetPtr() + header->FindSymbol("_BaseProbPtr")->value) = CRINKLER_BASEPROB;
	Symbol* modelSkipPtr = header->FindSymbol("_ModelSkipPtr");
	if (modelSkipPtr != nullptr) {
		assert(parts.GetNumInitializedParts() == 2);
		*(header->GetPtr() + modelSkipPtr->value) = parts.GetCodePart().m_model4k.nmodels + 2 + 4;
	}
	if (exportsRVA) {
		*(int*)(header->GetPtr() + header->FindSymbol("_ExportTableRVAPtr")->value) = exportsRVA;
		*(int*)(header->GetPtr() + header->FindSymbol("_NumberOfDataDirectoriesPtr")->value) = 1;
	}

	if (phase1->GetRawSize() >= 2 && (phase1->GetPtr()[0] == 0x5F || phase1->GetPtr()[2] == 0x5F))
	{
		// Code starts with POP EDI => call transform
		*(header->GetPtr() + header->FindSymbol("_SpareNopPtr")->value) = 0x57; // PUSH EDI
	}
}

void Crinkler::Recompress(const char* input_filename, const char* output_filename) {
	MemoryFile file(input_filename);
	unsigned char* indata = (unsigned char*)file.GetPtr();

	FILE* outfile = 0;
	if (strcmp(input_filename, output_filename) != 0) {
		// Open output file now, just to be sure
		if(fopen_s(&outfile, output_filename, "wb")) {
			Log::Error("", "Cannot open '%s' for writing", output_filename);
			return;
		}
	}

	int length = file.GetSize();
	if(length < 200)
	{
		NotCrinklerFileError();
	}

	unsigned int pe_header_offset = *(unsigned int*)&indata[0x3C];

	bool is_compatibility_header = false;
	
	bool is_tiny_header = false;
	char majorlv = 0, minorlv = 0;

	if(pe_header_offset == 4)
	{
		is_compatibility_header = false;
		majorlv = indata[2];
		minorlv = indata[3];
		if(majorlv >= '2' && indata[0xC] == 0x0F && indata[0xD] == 0xA3 && indata[0xE] == 0x2D)
		{
			is_tiny_header = true;
		}
	}
	else if(pe_header_offset == 12)
	{
		is_compatibility_header = true;
		majorlv = indata[38];
		minorlv = indata[39];
	}
	else
	{
		NotCrinklerFileError();
	}
	
	if (majorlv < '0' || majorlv > '9' ||
		minorlv < '0' || minorlv > '9') {
			NotCrinklerFileError();
	}

	// Oops: 0.6 -> 1.0
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
					Log::Error("", "Only files compressed using Crinkler 0.4 or newer can be recompressed.\n");
					return;
					break;
				case '4':
				case '5':
					FixHeader04((char*)indata);
					break;
			}
			break;
		case '1':
			switch(minorlv) {
				case '0':
					FixHeader10((char*)indata);
					break;
			}
			break;
	}

	PartList parts;

	CompressionType compmode = COMPRESSION_INSTANT;
	int virtualSize = (*(int*)&indata[pe_header_offset+0x50]) - 0x20000;
	int hashtable_size = -1;
	int return_offset = -1;
	int models_address = -1;
	int depacker_start = -1;
	int rawsize_start = -1;
	int compressed_data_rva = -1;
	for(int i = 0; i < 0x200; i++)
	{
		if(is_tiny_header)
		{
			if(indata[i] == 0x7C && indata[i + 2] == 0xC3 && return_offset == -1) {
				return_offset = i + 2;
				indata[return_offset] = 0xCC;
			}

			if(indata[i] == 0x66 && indata[i + 1] == 0x81 && indata[i + 2] == 0xff)
			{
				rawsize_start = i + 3;
			}

			ModelList1k& models = parts.GetCodePart().m_model1k;
			if(version <= 21)
			{
				if(indata[i] == 0xB9 && indata[i + 1] == 0x00 && indata[i + 2] == 0x00 && indata[i + 3] == 0x00 && indata[i + 4] == 0x00 &&
					indata[i + 5] == 0x59 && indata[i + 6] == 0x6a)
				{
					models.baseprob0 = indata[i + 7];
					models.baseprob1 = indata[i + 9];
					models.modelmask = *(unsigned int*)&indata[i + 11];
				}
			}
			else
			{
				if(indata[i] == 0x6a && indata[i + 2] == 0x3d && indata[i + 3] == 0x00 && indata[i + 4] == 0x00 && indata[i + 5] == 0x00 &&
					indata[i + 6] == 0x00 && indata[i + 7] == 0x6a )
				{
					models.baseprob0 = indata[i + 1];
					models.baseprob1 = indata[i + 8];
					models.modelmask = *(unsigned int*)&indata[i + 10];
				}
			}

			if(indata[i] == 0x7F && indata[i + 2] == 0xB1 && indata[i + 4] == 0x89 && indata[i + 5] == 0xE6)
			{
				models.boost = indata[i + 3];
			}

			if(indata[i] == 0x0F && indata[i + 1] == 0xA3 && indata[i + 2] == 0x2D && compressed_data_rva == -1)
			{
				compressed_data_rva = *(int*)&indata[i + 3];
			}
		}
		else
		{
			if(indata[i] == 0xbf && indata[i + 5] == 0xb9 && hashtable_size == -1) {
				hashtable_size = (*(int*)&indata[i + 6]) * 2;
			}
			if (version >= 30)
			{
				if (is_compatibility_header)
				{
					if (indata[i] == 0x8D && indata[i + 3] == 0x7B && indata[i + 5] == 0xC3 && return_offset == -1) {
						return_offset = i + 5;
						indata[return_offset] = 0xCC;
					}
				}
				else
				{
					if (indata[i] == 0x61 && indata[i + 1] == 0x5E && indata[i + 2] == 0x7B && indata[i + 4] == 0xC3 && return_offset == -1) {
						return_offset = i + 4;
						indata[return_offset] = 0xCC;
					}
				}
			}
			else
			{
				if (indata[i] == 0x5A && indata[i + 1] == 0x7B && indata[i + 3] == 0xC3 && return_offset == -1) {
					return_offset = i + 3;
					indata[return_offset] = 0xCC;
				}
				else if (indata[i] == 0x8D && indata[i + 3] == 0x7B && indata[i + 5] == 0xC3 && return_offset == -1) {
					return_offset = i + 5;
					indata[return_offset] = 0xCC;
				}
			}
			

			if(version < 13)
			{
				if(indata[i] == 0x4B && indata[i + 1] == 0x61 && indata[i + 2] == 0x7F) {
					depacker_start = i;
				}
			}
			else if(version == 13)
			{
				if(indata[i] == 0x0F && indata[i + 1] == 0xA3 && indata[i + 2] == 0x2D) {
					depacker_start = i;
				}
			}
			else if(version < 30)
			{
				if(indata[i] == 0xE8 && indata[i + 5] == 0x60 && indata[i + 6] == 0xAD) {
					depacker_start = i;
				}
			}
			else
			{
				if (indata[i] == 0xE8 && indata[i + 5] == 0x60 && indata[i + 6] == 0x66 && indata[i + 7] == 0xAD) {
					depacker_start = i;
				}
			}

			if(indata[i] == 0xBE && indata[i + 3] == 0x40 && indata[i + 4] == 0x00) {
				models_address = *(int*)&indata[i + 1];
			}
		}
	}

	int models_offset = -1;

	int rawsize = 0;
	std::vector<int> part_sizes;
	if(is_tiny_header)
	{
		if(return_offset == -1 && compressed_data_rva != -1)
		{
			NotCrinklerFileError();
		}

		rawsize = *(unsigned short*)&indata[rawsize_start];
		part_sizes.push_back(rawsize);
	}
	else
	{
		if(hashtable_size == -1 || return_offset == -1 || (depacker_start == -1 && is_compatibility_header) || models_address == -1)
		{
			NotCrinklerFileError();
		}

		models_offset = models_address - CRINKLER_IMAGEBASE;

		if (version >= 30)
		{
			int offset = models_offset;
			unsigned short prev_part_end_pos = 0;

			bool done = false;
			int part_index = 0;
			while(!done)
			{
				char custom_name[128];
				if (part_index >= 1) {
					sprintf(custom_name, "Part%d", part_index);
				}
				
				Part& part = parts.GetOrAddPart(	part_index == 0 ? "Code" :
													custom_name, true);

				unsigned short part_end_pos = -*(unsigned short*)&indata[offset];
				unsigned int weightmask = *(unsigned int*)&indata[offset + 2];

				unsigned short part_size = part_end_pos - prev_part_end_pos;
				prev_part_end_pos = part_end_pos;

				part.m_model4k.SetFromModelsAndMask(indata + offset + 6, weightmask, &done);
				offset += 6 + part.m_model4k.nmodels;
				rawsize += part_size;
				part_index++;
				
				part_sizes.push_back(part_size);
			}
		}
		else
		{
			ModelList4k& codeModels = parts.GetCodePart().m_model4k;
			ModelList4k& dataModels = parts.GetOrAddPart("Data", true).m_model4k;
	
			unsigned int weightmask1 = *(unsigned int*)&indata[models_offset + 4];
			unsigned char* models1 = &indata[models_offset + 8];
			codeModels.SetFromModelsAndMask(models1, weightmask1);
			int modelskip = 8 + codeModels.nmodels;
			unsigned int weightmask2 = *(unsigned int*)&indata[models_offset + modelskip + 4];
			unsigned char* models2 = &indata[models_offset + modelskip + 8];
			dataModels.SetFromModelsAndMask(models2, weightmask2);

			int splittingPoint;
			if (version >= 13) {
				rawsize = -(*(int*)&indata[models_offset + modelskip]) - CRINKLER_CODEBASE;
				splittingPoint = -(*(int*)&indata[models_offset]) - CRINKLER_CODEBASE;
			}
			else
			{
				rawsize = (*(int*)&indata[models_offset + modelskip]) / 8;
				splittingPoint = (*(int*)&indata[models_offset]) / 8;
			}
			part_sizes.push_back(splittingPoint);
			part_sizes.push_back(rawsize - splittingPoint);
		}

		compmode = parts.GetCodePart().m_model4k.DetectCompressionType();
	}

	// Update parts with part sizes
	{
		int offset = 0;
		for(unsigned part_index = 0; part_index < part_sizes.size(); part_index++) {
			Part& part = parts[part_index];
			const int part_size = part_sizes[part_index];
			part.SetLinkedOffset(offset);
			part.SetLinkedSize(part_size);
			offset += part_size;
		}
		Part& part = parts.GetUninitializedPart();
		part.SetLinkedOffset(offset);
		part.SetLinkedSize(virtualSize - offset);
	}

	SetUseTinyHeader(is_tiny_header);
	
	int subsystem_version = indata[pe_header_offset+0x5C];
	int large_address_aware = (*(unsigned short *)&indata[pe_header_offset+0x16] & 0x0020) != 0;

	static const unsigned char saturateCode[] = { 0x75, 0x03, 0xFE, 0x0C, 0x1F };
	bool saturate = std::search(indata, indata + length, std::begin(saturateCode), std::end(saturateCode)) != indata + length;
	if (m_saturate == -1) m_saturate = saturate;

	int exports_rva = 0;
	if(!is_tiny_header && majorlv >= '2')
	{
		exports_rva = *(int*)&indata[pe_header_offset + 0x78];
	}
	
	printf("Original file size: %d\n", length);
	printf("Original Tiny Header: %s\n", is_tiny_header ? "YES" : "NO");
	printf("Original Virtual size: %d\n", virtualSize);
	printf("Original Subsystem type: %s\n", subsystem_version == 3 ? "CONSOLE" : "WINDOWS");
	printf("Original Large address aware: %s\n", large_address_aware ? "YES" : "NO");
	if(!is_tiny_header)
	{
		printf("Original Compression mode: %s\n", compmode == COMPRESSION_INSTANT ? "INSTANT" : version < 21 ? "FAST/SLOW" : "FAST/SLOW/VERYSLOW");
		printf("Original Saturate counters: %s\n", saturate ? "YES" : "NO");
		printf("Original Hash size: %d\n", hashtable_size);
	}
	
	if(is_tiny_header)
	{
		printf("Total size: %d\n", rawsize);
		printf("\n");
	}
	else
	{
		parts.ForEachPart([](const Part& part, int index) {
			printf("%s size: %d\n", part.GetName(), part.GetLinkedSize());
			});
		
		printf("\n");
	}
	
	STARTUPINFO startupInfo = {0};
	startupInfo.cb = sizeof(startupInfo);

	char tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	char tempFilename[MAX_PATH];

	GetTempFileName(tempPath, "", 0, tempFilename);
	PROCESS_INFORMATION pi;

	if(!file.Write(tempFilename)) {
		Log::Error("", "Failed to write to temporary file '%s'\n", tempFilename);
	}

	CreateProcess(tempFilename, NULL, NULL, NULL, false, NORMAL_PRIORITY_CLASS|CREATE_SUSPENDED, NULL, NULL, &startupInfo, &pi);
	DebugActiveProcess(pi.dwProcessId);
	ResumeThread(pi.hThread);

	bool done = false;
	do {
		DEBUG_EVENT de;
		if(WaitForDebugEvent(&de, 120000) == 0) {
			Log::Error("", "Program was been unresponsive for more than 120 seconds - closing down\n");
		}

		if(de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT && 
			(de.u.Exception.ExceptionRecord.ExceptionAddress == (PVOID)(size_t)(0x410000+return_offset) ||
			de.u.Exception.ExceptionRecord.ExceptionAddress == (PVOID)(size_t)(0x400000+return_offset)))
		{
			done = true;
		}

		if(!done)
			ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
	} while(!done);

	unsigned char* rawdata = new unsigned char[rawsize];
	SIZE_T read;
	if(ReadProcessMemory(pi.hProcess, (LPCVOID)0x420000, rawdata, rawsize, &read) == 0 || read != rawsize) {
		Log::Error("", "Failed to read process memory\n");
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	// Patch calltrans code
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

	// Patch import code
	static const unsigned char old_import_code[] = {0x31, 0xC0, 0x64, 0x8B, 0x40, 0x30, 0x8B, 0x40, 
													0x0C, 0x8B, 0x40, 0x1C, 0x8B, 0x40, 0x00, 0x8B,
													0x68, 0x08};
	static const unsigned char new_import_code[] = {0x64, 0x67, 0x8B, 0x47, 0x30, 0x8B, 0x40, 0x0C,
													0x8B, 0x40, 0x0C, 0x8B, 0x00, 0x8B, 0x00, 0x8B,
													0x68, 0x18};
	static const unsigned char new_import_code2[] ={0x58, 0x8B, 0x40, 0x0C, 0x8B, 0x40, 0x0C, 0x8B,
													0x00, 0x8B, 0x00, 0x8B, 0x68, 0x18};
	static const unsigned char tiny_import_code[] ={0x58, 0x8B, 0x40, 0x0C, 0x8B, 0x40, 0x0C, 0x8B,
													0x40, 0x00, 0x8B, 0x40, 0x00, 0x8B, 0x40, 0x18 };
	bool found_import = false;
	int hashes_address = -1;
	int hashes_address_offset = -1;
	bool is_tiny_import = false;

	for (int i = import_offset ; i < part_sizes[0] - (int)sizeof(old_import_code); i++) {
		if (rawdata[i] == 0xBB) {
			hashes_address_offset = i + 1;
			hashes_address = *(int*)&rawdata[hashes_address_offset];
		}

		if (memcmp(rawdata+i, old_import_code, sizeof(old_import_code)) == 0) {			// No calltrans
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
		
		if(memcmp(rawdata + i, tiny_import_code, sizeof(tiny_import_code)) == 0)
		{
			printf("Import code does not need patching.\n");
			found_import = true;
			is_tiny_import = true;
			break;
		}
	}
	
	if(!found_import)
	{
		Log::Error("", "Cannot find old import code to patch\n");
	}

	SetUseTinyImport(is_tiny_import);

	printf("\n");

	Part headerPart("Header", true);
	if(is_tiny_header)
	{
		m_hunkLoader.Load(headerPart, header1KObj, int(header1KObj_end - header1KObj), "crinkler header", false);
	}
	else
	{
		if(is_compatibility_header)
		{
			m_hunkLoader.Load(headerPart, headerCompatibilityObj, int(headerCompatibilityObj_end - headerCompatibilityObj), "crinkler header");
		}
		else
		{
			m_hunkLoader.Load(headerPart, headerObj, int(headerObj_end - headerObj), "crinkler header");
		}
	}
	
	Hunk* header = headerPart.FindSymbol("_header")->hunk;
	Hunk* depacker = nullptr;
	
	if (is_compatibility_header) {
		depacker = headerPart.FindSymbol("_DepackEntry")->hunk;
		SetHeaderSaturation(depacker);
	}

	if(!is_tiny_import)
	{
		int new_hashes_address = is_compatibility_header ? CRINKLER_IMAGEBASE : CRINKLER_IMAGEBASE + header->GetRawSize();
		*(int*)&rawdata[hashes_address_offset] = new_hashes_address;
	}
	
	Hunk* phase1 = new Hunk("linked", (char*)rawdata, HUNK_IS_CODE|HUNK_IS_WRITEABLE, 0, rawsize, virtualSize);

	delete[] rawdata;

	if(!is_tiny_header)
	{
		// Handle exports
		std::set<Export> exports;
		printf("Original Exports:");
		if(exports_rva) {
			exports = StripExports(phase1, exports_rva);
			printf("\n");
			PrintExports(exports);
			if(!m_stripExports) {
				for(const Export& e : exports) {
					AddExport(e);
				}
			}
		}
		else {
			printf(" NONE\n");
		}
		printf("Resulting Exports:");
		if(!m_exports.empty()) {
			printf("\n");
			PrintExports(m_exports);
			for(const Export& e : m_exports) {
				if(!e.HasValue()) {
					Symbol *sym = phase1->FindSymbol(e.GetSymbol().c_str());
					if(!sym) {
						Log::Error("", "Cannot find symbol '%s' to be exported under name '%s'.", e.GetSymbol().c_str(), e.GetName().c_str());
					}
				}
			}
			
			int padding = exports_rva ? 0 : 16;
			phase1->SetVirtualSize(phase1->GetRawSize() + padding);
			Hunk* export_hunk = CreateExportTable(m_exports);
			PartList temp_partlist;
			temp_partlist.GetCodePart().AddHunkBack(phase1);
			temp_partlist.GetCodePart().AddHunkBack(export_hunk);
			Hunk* with_exports = temp_partlist.Link("linked", CRINKLER_CODEBASE);
			with_exports->SetVirtualSize(virtualSize);
			with_exports->Relocate(CRINKLER_CODEBASE);
			delete phase1;
			phase1 = with_exports;
		}
		else {
			printf(" NONE\n");
		}
	}

	{
		phase1->Trim();
		
		Part& lastInitializedPart = parts[parts.GetNumInitializedParts() - 1];
		lastInitializedPart.SetLinkedSize(phase1->GetRawSize() - lastInitializedPart.GetLinkedOffset());
		parts.GetUninitializedPart().SetLinkedOffset(phase1->GetRawSize());
		parts.GetUninitializedPart().SetLinkedSize(phase1->GetVirtualSize() - phase1->GetRawSize());
	}

	parts.ForEachPart([phase1](const Part& part, int index) {
		int flags = SYMBOL_IS_RELOCATEABLE | SYMBOL_IS_SECTION | SYMBOL_IS_LOCAL;
		if (index == 0)
			flags |= SYMBOL_IS_CODE;
		Symbol* s = new Symbol(part.GetName(), part.GetLinkedOffset(), flags, phase1);
		s->hunkOffset = 0;
		phase1->AddSymbol(s);
		});

	printf("\nRecompressing...\n");

	int maxsize = phase1->GetRawSize()*2+1000;

	int* sizefill = new int[maxsize];

	unsigned char* data = new unsigned char[maxsize];
	int best_hashsize = 0;
	int size;
	if(is_tiny_header)
	{
		size = Compress1k((unsigned char*)phase1->GetPtr(), phase1->GetRawSize(), data, maxsize, parts.GetCodePart().m_model1k, sizefill, nullptr);
		printf("Real compressed total size: %d\n", size);
	}
	else
	{
		int idealsize = 0;
		if(m_compressionType < 0)
		{
			// Keep models
			if(m_hashsize < 0) {
				// Use original optimized hash size
				SetHashsize((hashtable_size - 1) / (1024 * 1024) + 1);
				best_hashsize = hashtable_size;
				SetHashtries(0);
			}
			else {
				best_hashsize = PreviousPrime(m_hashsize / 2) * 2;
				InitProgressBar();

				// Rehash
				best_hashsize = OptimizeHashsize(parts, phase1, best_hashsize, m_hashtries);
				DeinitProgressBar();
			}
		}
		else {
			if(m_hashsize < 0) {
				SetHashsize((hashtable_size - 1) / (1024 * 1024) + 1);
			}
			best_hashsize = PreviousPrime(m_hashsize / 2) * 2;
			if(m_compressionType != COMPRESSION_INSTANT) {
				InitProgressBar();
				idealsize = EstimateModels(parts, phase1, false, false);

				// Hashing
				best_hashsize = OptimizeHashsize(parts, phase1, best_hashsize, m_hashtries);
				DeinitProgressBar();
			}
		}
		
		size = CompressParts4k(parts, phase1, data, maxsize, best_hashsize, sizefill);

		if(m_compressionType != -1 && m_compressionType != COMPRESSION_INSTANT) {
			int sizeIncludingModels = size + parts.CalcTotalPartAndModelOverhead();
			float byteslost = sizeIncludingModels - idealsize / (float)(BIT_PRECISION * 8);
			printf("Real compressed total size: %d\nBytes lost to hashing: %.2f\n", sizeIncludingModels, byteslost);
		}
		
		SetCompressionType(compmode);
	}

	if(is_compatibility_header)
	{	// Copy hashes from old header
		DWORD* new_header_ptr = (DWORD*)header->GetPtr();
		DWORD* old_header_ptr = (DWORD*)indata;

		for(int i = 0; i < depacker_start / 4; i++) {
			if(new_header_ptr[i] == 'HSAH')
				new_header_ptr[i] = old_header_ptr[i];
		}
		header->SetRawSize(depacker_start);
		header->SetVirtualSize(depacker_start);
	}

	Hunk *hashHunk = nullptr;
	if (!is_compatibility_header && !is_tiny_import)
	{
		// Create hunk with hashes
		int hashes_offset = hashes_address - CRINKLER_IMAGEBASE;
		int hashes_bytes = is_tiny_header ? (compressed_data_rva - CRINKLER_IMAGEBASE - hashes_offset) : (models_offset - hashes_offset);
		hashHunk = new Hunk("HashHunk", (char*)&indata[hashes_offset], 0, 0, hashes_bytes, hashes_bytes);
	}

	if (m_subsystem >= 0) {
		subsystem_version = (m_subsystem == SUBSYSTEM_WINDOWS) ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI;
	}
	if (m_largeAddressAware == -1) {
		m_largeAddressAware = large_address_aware;
	}
	SetSubsystem((subsystem_version == IMAGE_SUBSYSTEM_WINDOWS_GUI) ? SUBSYSTEM_WINDOWS : SUBSYSTEM_CONSOLE);

	Hunk* phase2 = FinalLink(parts, header, depacker, hashHunk, phase1, data, size, best_hashsize);
	delete[] data;
	
	CompressionReportRecord* csr = phase1->GenerateCompressionSummary(parts, sizefill);
	if(m_printFlags & PRINT_LABELS)
		VerboseLabels(csr);
	if(!m_summaryFilename.empty())
		HtmlReport(csr, m_summaryFilename.c_str(), *phase1, *phase1, sizefill,
			output_filename, phase2->GetRawSize(), this);
	delete csr;
	delete[] sizefill;

	if (!outfile) {
		if(fopen_s(&outfile, output_filename, "wb")) {
			Log::Error("", "Cannot open '%s' for writing", output_filename);
			return;
		}
	}
	fwrite(phase2->GetPtr(), 1, phase2->GetRawSize(), outfile);
	fclose(outfile);

	printf("\nOutput file: %s\n", output_filename);
	printf("Final file size: %d\n\n", phase2->GetRawSize());

	delete phase1;
	delete phase2;
}

Hunk* Crinkler::CreateDynamicInitializerHunk()
{
	std::vector<Symbol*> symbols;
	m_hunkList.ForEachHunk([this, &symbols](Hunk* hunk)
		{
			if (EndsWith(hunk->GetName(), "CRT$XCU"))
			{
				int num_relocations = hunk->GetNumRelocations();
				Relocation* relocations = hunk->GetRelocations();
				for (int i = 0; i < num_relocations; i++)
				{
					symbols.push_back(m_hunkList.FindSymbol(relocations[i].symbolname.c_str()));
				}
			}
		});
	
	if(!symbols.empty())
	{
		const int num_symbols = (int)symbols.size();
		const int hunk_size = num_symbols*5;
		Hunk* hunk = new Hunk("dynamic initializer calls", NULL, HUNK_IS_CODE, 0, hunk_size, hunk_size);

		unsigned char* ptr = hunk->GetPtr();
		for(int i = 0; i < num_symbols; i++)
		{
			*ptr++ = (char)0xE8;
			*ptr++ = 0x00;
			*ptr++ = 0x00;
			*ptr++ = 0x00;
			*ptr++ = 0x00;
			
			Relocation r;
			r.offset = i*5+1;
			r.symbolname = symbols[i]->name;
			r.type = RELOCTYPE_REL32;
			hunk->AddRelocation(r);
		}
		hunk->AddSymbol(new Symbol("__DynamicInitializers", 0, SYMBOL_IS_RELOCATEABLE, hunk));
		printf("\nIncluded %d dynamic initializer%s.\n", num_symbols, num_symbols == 1 ? "" : "s");
		return hunk;
	}
	return NULL;
}

void Crinkler::Link(const char* filename) {
	// Open output file immediate, just to be sure
	FILE* outfile;
	int old_filesize = 0;
	if (!fopen_s(&outfile, filename, "rb")) {
		// Find old size
		fseek(outfile, 0, SEEK_END);
		old_filesize = ftell(outfile);
		fclose(outfile);
	}
	if(fopen_s(&outfile, filename, "wb")) {
		Log::Error("", "Cannot open '%s' for writing", filename);
		return;
	}


	// Find entry hunk and move it to front
	Symbol* entry = FindEntryPoint();
	if(entry == NULL)
		return;

	Hunk* dynamicInitializersHunk = NULL;
	if (m_runInitializers) {
		dynamicInitializersHunk = CreateDynamicInitializerHunk();
		if(dynamicInitializersHunk)
		{
			m_hunkList.AddHunkBack(dynamicInitializersHunk);
		}
	}

	// Color hunks from entry hunk
	RemoveUnreferencedHunks(entry->hunk);

	// Replace DLLs
	ReplaceDlls(m_hunkList);

	if (m_overrideAlignments) OverrideAlignments(m_hunkList);

	// 1-byte align entry point and other sections
	int n_unaligned = 0;
	bool entry_point_unaligned = false;
	if(entry->hunk->GetAlignmentBits() > 0) {
		entry->hunk->SetAlignmentBits(0);
		n_unaligned++;
		entry_point_unaligned = true;
	}
	if (m_unalignCode) {
		m_hunkList.ForEachHunk([&n_unaligned](Hunk* hunk)
			{
				if (hunk->GetFlags() & HUNK_IS_CODE && !(hunk->GetFlags() & HUNK_IS_ALIGNED) && hunk->GetAlignmentBits() > 0) {
					hunk->SetAlignmentBits(0);
					n_unaligned++;
				}
			});
	}
	if (n_unaligned > 0) {
		printf("Forced alignment of %d code hunk%s to 1", n_unaligned, n_unaligned > 1 ? "s" : "");
		if (entry_point_unaligned) {
			printf(" (including entry point)");
		}
		printf(".\n");
	}

	// Load appropriate header
	Part headerPart("Header", true);

	if (m_useTinyHeader)
		m_hunkLoader.Load(headerPart, header1KObj, int(header1KObj_end - header1KObj), "crinkler header");
	else
		m_hunkLoader.Load(headerPart, headerObj, int(headerObj_end - headerObj), "crinkler header");

	Hunk* header = headerPart.FindSymbol("_header")->hunk;

	if(!m_useTinyHeader)
		SetHeaderSaturation(header);
	Hunk* hashHunk = NULL;

	int hash_bits;
	int max_dll_name_length;
	bool usesRangeImport=false;
	{	// Add imports
		if (m_useTinyImport)
			ImportHandler::AddImportHunks1K(m_hunkList, (m_printFlags & PRINT_IMPORTS) != 0, hash_bits, max_dll_name_length);
		else
			ImportHandler::AddImportHunks4K(m_hunkList, hashHunk, m_fallbackDlls, m_rangeDlls, (m_printFlags & PRINT_IMPORTS) != 0, usesRangeImport);
	}

	LoadImportCode(m_useTinyImport, m_useSafeImporting, !m_fallbackDlls.empty(), usesRangeImport);

	Symbol* importSymbol = m_hunkList.FindSymbol("_Import");

	if(dynamicInitializersHunk)
	{		
		m_hunkList.RemoveHunk(dynamicInitializersHunk);
		m_hunkList.AddHunkFront(dynamicInitializersHunk);
		dynamicInitializersHunk->SetContinuation(entry);
	}

	Hunk* importHunk = importSymbol->hunk;

	m_hunkList.RemoveHunk(importHunk);
	m_hunkList.AddHunkFront(importHunk);
	importHunk->SetAlignmentBits(0);
	importHunk->SetContinuation(dynamicInitializersHunk ? dynamicInitializersHunk->FindSymbol("__DynamicInitializers") : entry);
	
	// Make sure import and startup code has access to the _ImageBase address
	importHunk->AddSymbol(new Symbol("_ImageBase", CRINKLER_IMAGEBASE, 0, importHunk));
	importHunk->AddSymbol(new Symbol("___ImageBase", CRINKLER_IMAGEBASE, 0, importHunk));

	if(m_useTinyImport)
	{
		*(importHunk->GetPtr() + importHunk->FindSymbol("_HashShiftPtr")->value) = 32 - hash_bits;
		*(importHunk->GetPtr() + importHunk->FindSymbol("_MaxNameLengthPtr")->value) = max_dll_name_length;
	}

	// Truncate floats
	if(m_truncateFloats) {
		printf("\nTruncating floats:\n");

		m_hunkList.ForEachHunk([this](Hunk* hunk) { hunk->RoundFloats(m_truncateBits); });
	}

	int best_hashsize = PreviousPrime(m_hashsize / 2) * 2;

	PartList parts;

	bool reuse_mismatch = false;
	int reuse_filesize = 0;
	Reuse *reuse = nullptr;
	ReuseType reuseType = m_useTinyHeader ? REUSE_OFF : m_reuseType;
	if (reuseType != REUSE_OFF && reuseType != REUSE_NOTHING) {
		reuse = LoadReuseFile(m_reuseFilename.c_str());
		if (reuse == nullptr) {
			reuseType = REUSE_NOTHING;
		} else {
			printf("\nRead reuse file: %s\n", m_reuseFilename.c_str());
			if (reuseType == REUSE_ASK) {
				reuseType = AskForReuseMode();
				printf("\nSelected reuse mode: %s\n", ReuseTypeName(reuseType));
				if (reuseType == REUSE_OFF || reuseType == REUSE_NOTHING) {
					delete reuse;
					reuse = nullptr;
				}
			}
		}
	}

	if (reuse != nullptr) {
		reuse_mismatch = reuse->PartsFromHunkList(parts, m_hunkList);
		best_hashsize = reuse->GetHashSize();
		if (reuse_mismatch) {
			delete reuse;
			reuse = new Reuse(parts, best_hashsize);
		}
	}
	else
	{
		// Split hunks into separate parts
		if (!m_useTinyHeader) {
			parts.GetOrAddPart("Data", true);

			if (m_textPart == TEXT_PART_YES || m_textPart == TEXT_PART_AUTO) {
				int totalTextSize = 0;
				m_hunkList.ForEachHunk([&totalTextSize](Hunk* hunk) {
					if (hunk->IsLikelyText()) {
						totalTextSize += hunk->GetRawSize();
					}
					});

				printf("Total text size: %d\n", totalTextSize);
				if(m_textPart == TEXT_PART_YES || totalTextSize >= 500)
					parts.GetOrAddPart("Text", true);
			}
		}

		m_hunkList.RemoveMatchingHunks([&parts](Hunk* hunk) {
			int partIndex = parts.FindBestPartIndex(hunk);
			parts[partIndex].AddHunkBack(hunk);
			return true;
		});

		// Sort hunks heuristically
		HeuristicHunkSorter::SortHunkList(parts);
	}

	// Add export table after heuristic sorting and reuse to make sure it is last
	if (!m_exports.empty()) {
		parts[parts.GetNumParts() - 2].AddHunkBack(CreateExportTable(m_exports));
	}

	// Create phase 1 data hunk
	Hunk* phase1, *phase1Untransformed;
	parts.GetCodePart()[0]->AddSymbol(new Symbol("_HeaderHashes", CRINKLER_IMAGEBASE + header->GetRawSize(), SYMBOL_IS_SECTION, parts.GetCodePart()[0]));

	if (!m_transform->LinkAndTransform(parts, importSymbol, CRINKLER_CODEBASE, &phase1, &phase1Untransformed, true))
	{
		// Transform failed, run again
		delete phase1;
		delete phase1Untransformed;
		m_transform->LinkAndTransform(parts, importSymbol, CRINKLER_CODEBASE, &phase1, &phase1Untransformed, false);
	}
	int maxsize = phase1->GetRawSize()*2+1000;	// Allocate plenty of memory	
	unsigned char* data = new unsigned char[maxsize];

	if (reuseType != REUSE_OFF && reuseType != REUSE_NOTHING) {
		int size = CompressParts4k(parts, phase1, data, maxsize, best_hashsize, nullptr);
		Hunk *phase2 = FinalLink(parts, header, nullptr, hashHunk, phase1, data, size, best_hashsize);
		reuse_filesize = phase2->GetRawSize();
		delete phase2;

		printf("\nFile size with reuse configuration: %d\n", reuse_filesize);
	}

	// Print uncompressed part sizes
	{
		int longestName = 0;
		parts.ForEachPart([&longestName](const Part& part, int index) {
			longestName = std::max(longestName, (int)strlen(part.GetName()));
			});

		parts.ForEachPart([longestName](const Part& part, int index) {
			char nameWithColon[512];
			sprintf(nameWithColon, "%s:", part.GetName());
			printf("\nUncompressed size of %-*s %10d", longestName + 1, nameWithColon, part.GetLinkedSize());
			});
		printf("\n");
	}

	int* sizefill = new int[maxsize];
	int size, idealsize = 0;
	if (m_useTinyHeader || m_compressionType != COMPRESSION_INSTANT)
	{
		if (reuseType != REUSE_ALL) InitProgressBar();

		if (reuseType == REUSE_MODELS || reuseType == REUSE_ALL) {
			// Calculate ideal size with reuse parameters
			idealsize = EvaluatePartsSize4k(parts, phase1);
		} else {
			// Full size estimation and hunk reordering
			idealsize = EstimateModels(parts, phase1, false, m_useTinyHeader);

			if (m_hunktries > 0 && reuseType != REUSE_SECTIONS)
			{
				EmpiricalHunkSorter::SortHunkList(parts, *m_transform, m_saturate != 0, m_hunktries, m_showProgressBar ? &m_windowBar : NULL, m_useTinyHeader);
				delete phase1;
				delete phase1Untransformed;
				m_transform->LinkAndTransform(parts, importSymbol, CRINKLER_CODEBASE, &phase1, &phase1Untransformed, false);

				idealsize = EstimateModels(parts, phase1, true, m_useTinyHeader);
			}
		}

		// Hashing time
		if (!m_useTinyHeader && reuseType != REUSE_ALL)
		{
			best_hashsize = PreviousPrime(m_hashsize / 2) * 2;
			best_hashsize = OptimizeHashsize(parts, phase1, best_hashsize, m_hashtries);
		}

		if (reuseType != REUSE_ALL) DeinitProgressBar();
	}

	if (m_useTinyHeader)
	{
		size = Compress1k((unsigned char*)phase1->GetPtr(), phase1->GetRawSize(),data, maxsize, parts.GetCodePart().m_model1k, sizefill, nullptr);
	}
	else
	{
		size = CompressParts4k(parts, phase1, data, maxsize, best_hashsize, sizefill);
	}
	
	if(!m_useTinyHeader && m_compressionType != COMPRESSION_INSTANT) {
		int sizeIncludingModels = size + parts.CalcTotalPartAndModelOverhead();
		float byteslost = sizeIncludingModels - idealsize / (float) (BIT_PRECISION * 8);
		printf("Real compressed total size: %d\nBytes lost to hashing: %.2f\n", sizeIncludingModels, byteslost);
	}

	Hunk *phase2 = FinalLink(parts, header, nullptr, hashHunk, phase1, data, size, best_hashsize);

	printf("\n");

	if (reuseType != REUSE_OFF) {
		bool save_reuse = false;
		if (reuse == nullptr) {
			printf("Writing reuse file: %s\n", m_reuseFilename.c_str());
			reuse = new Reuse(parts, best_hashsize);
			save_reuse = true;
		} else if (reuseType != REUSE_ALL && phase2->GetRawSize() < reuse_filesize) {
			printf("Overwriting reuse file: %s\n", m_reuseFilename.c_str());
			delete reuse;
			reuse = new Reuse(parts, best_hashsize);
			save_reuse = true;
		} else {
			if (reuseType != REUSE_ALL) {
				printf("File size: %d\n", phase2->GetRawSize());
				printf("Reverting to reuse configuration.\n");
				parts.Clear();
				bool new_reuse_mismatch = reuse->PartsFromHunkList(parts, m_hunkList);
				assert(!new_reuse_mismatch);
				delete phase1;
				delete phase1Untransformed;
				m_transform->LinkAndTransform(parts, importSymbol, CRINKLER_CODEBASE, &phase1, &phase1Untransformed, false);
				int size = CompressParts4k(parts, phase1, data, maxsize, reuse->GetHashSize(), nullptr);
				delete phase2;
				phase2 = FinalLink(parts, header, nullptr, hashHunk, phase1, data, size, best_hashsize);
			}

			if (reuse_mismatch) {
				printf("Updating reuse file: %s\n", m_reuseFilename.c_str());
				save_reuse = true;
			} else if (reuseType != REUSE_ALL) {
				printf("Keeping reuse file: %s\n", m_reuseFilename.c_str());
			}
		}

		if (save_reuse) reuse->Save(m_reuseFilename.c_str());
	}

	delete[] data;

	CompressionReportRecord* csr = phase1->GenerateCompressionSummary(parts, sizefill);
	if(m_printFlags & PRINT_LABELS)
		VerboseLabels(csr);
	if(!m_summaryFilename.empty())
		HtmlReport(csr, m_summaryFilename.c_str(), *phase1, *phase1Untransformed, sizefill,
			filename, phase2->GetRawSize(), this);
	delete csr;
	delete[] sizefill;
	
	fwrite(phase2->GetPtr(), 1, phase2->GetRawSize(), outfile);
	fclose(outfile);

	printf("\nOutput file: %s\n", filename);
	printf("Final file size: %d", phase2->GetRawSize());
	if (old_filesize)
	{
		if (old_filesize != phase2->GetRawSize()) {
			printf(" (previous size %d)", old_filesize);
		} else {
			printf(" (no change)");
		}
	}
	printf("\n\n");

	if (phase2->GetRawSize() > 128*1024)
	{
		Log::Error(filename, "Output file too big. Crinkler does not support final file sizes of more than 128k.");
	}

	if (reuse) delete reuse;
	delete phase1;
	delete phase1Untransformed;
	delete phase2;
}

int	Crinkler::EvaluatePartsSize4k(PartList& parts, Hunk* phase1) {
	const int numInitializedParts = parts.GetNumInitializedParts();
	std::vector<ModelList4k*> modelLists(numInitializedParts);
	std::vector<int> partSizes(numInitializedParts);
	std::vector<int> compressedPartSizes(numInitializedParts);

	for (int i = 0; i < numInitializedParts; i++)
	{
		Part& part = parts[i];
		modelLists[i] = &part.m_model4k;
		partSizes[i] = part.GetLinkedSize();
	}

	int size = EvaluateSize4k(phase1->GetPtr(), numInitializedParts, partSizes.data(), compressedPartSizes.data(), modelLists.data(), CRINKLER_BASEPROB, m_saturate != 0);

	printf("\n");
	for (int i = 0; i < numInitializedParts; i++)
	{
		printf("Ideal compressed size of %s: %.2f\n", parts[i].GetName(), compressedPartSizes[i] / (float)(BIT_PRECISION * 8));
	}
	printf("Ideal compressed total size: %.2f\n", size / (float)(BIT_PRECISION * 8));

	return size;
}

int Crinkler::CompressParts4k(PartList& parts, Hunk* phase1, unsigned char* outCompressedData, int maxCompressedSize, int hashsize, int* sizefill) {
	const int numInitializedParts = parts.GetNumInitializedParts();
	std::vector<ModelList4k*> modelLists(numInitializedParts);
	std::vector<int> partSizes(numInitializedParts);

	for (int i = 0; i < numInitializedParts; i++)
	{
		Part& part = parts[i];
		modelLists[i] = &part.m_model4k;
		partSizes[i] = part.GetLinkedSize();
	}

	return Compress4k((unsigned char*)phase1->GetPtr(), numInitializedParts, partSizes.data(), outCompressedData, maxCompressedSize, modelLists.data(), m_saturate != 0, CRINKLER_BASEPROB, hashsize, sizefill);
}


Hunk *Crinkler::FinalLink(PartList& parts, Hunk *header, Hunk *depacker, Hunk *hashHunk, Hunk *phase1, unsigned char *data, int size, int hashsize)
{
	Hunk* phase1Compressed = new Hunk("compressed data", (char*)data, 0, 0, size, size);
	phase1Compressed->AddSymbol(new Symbol("_PackedData", 0, SYMBOL_IS_RELOCATEABLE, phase1Compressed));

	Hunk *modelHunk = nullptr;
	if (!m_useTinyHeader)
	{
		header->AddSymbol(new Symbol("_HashTable", CRINKLER_SECTIONSIZE * 2 + phase1->GetRawSize(), SYMBOL_IS_RELOCATEABLE, header));
		modelHunk = CreateModelHunk4k(parts);
	}

	PartList phase2parts;
	Part& part = phase2parts.GetCodePart();

	part.AddHunkBack(new Hunk(*header));
	if (depacker) part.AddHunkBack(new Hunk(*depacker));
	if (hashHunk) part.AddHunkBack(new Hunk(*hashHunk));
	if (modelHunk) part.AddHunkBack(modelHunk);
	part.AddHunkBack(phase1Compressed);

	Hunk* phase2 = phase2parts.Link("final", CRINKLER_IMAGEBASE);
	int subsystemVersion = (m_subsystem == SUBSYSTEM_WINDOWS ? IMAGE_SUBSYSTEM_WINDOWS_GUI : IMAGE_SUBSYSTEM_WINDOWS_CUI);

	// Add constants
	if (m_useTinyHeader)
	{
		ModelList1k& model = parts.GetCodePart().m_model1k;
		SetHeaderConstants1k(phase2, phase1, model.boost, model.baseprob0, model.baseprob1, model.modelmask, subsystemVersion);
	}
	else
	{
		int exportsRVA = m_exports.empty() ? 0 : phase1->FindSymbol("_ExportTable")->value + CRINKLER_CODEBASE - CRINKLER_IMAGEBASE;
		SetHeaderConstants4k(phase2, phase1, parts, hashsize, subsystemVersion, exportsRVA);
	}
	
	phase2->Relocate(CRINKLER_IMAGEBASE);

	return phase2;
}

void Crinkler::PrintOptions(back_insert_iterator<vector<char>> out) {
	format_to(out, " /SUBSYSTEM:{}", m_subsystem == SUBSYSTEM_CONSOLE ? "CONSOLE" : "WINDOWS");
	if (m_largeAddressAware) {
		format_to(out, " /LARGEADDRESSAWARE");
	}
	if (!m_entry.empty()) {
		format_to(out, " /ENTRY:{}", m_entry);
	}
	if(m_useTinyHeader) {
		format_to(out, " /TINYHEADER");
	}
	if(m_useTinyImport) {
		format_to(out, " /TINYIMPORT");
	}
	
	if(!m_useTinyHeader)
	{
		format_to(out, " /COMPMODE:{}", CompressionTypeName(m_compressionType));
		if (m_saturate) {
			format_to(out, " /SATURATE");
		}
		format_to(out, " /HASHSIZE:{}", m_hashsize / 1048576);
	}
	
	if (m_compressionType != COMPRESSION_INSTANT) {
		if (m_hashtries != 500)
		{
			format_to(out, " /HASHTRIES:{}", m_hashtries);
		}
		format_to(out, " /ORDERTRIES:{}", m_hunktries);
	}
	for(int i = 0; i < (int)m_rangeDlls.size(); i++) {
		format_to(out, " /RANGE:{}", m_rangeDlls[i]);
	}
	for(const auto& p : m_replaceDlls) {
		format_to(out, " /REPLACEDLL:{}={}", p.first, p.second);
	}
	for (const auto& p : m_fallbackDlls) {
		format_to(out, " /FALLBACKDLL:{}={}", p.first, p.second);
	}
	if (!m_useTinyHeader && !m_useSafeImporting) {
		format_to(out, " /UNSAFEIMPORT");
	}
	if (m_transform->GetDetransformer() != NULL) {
		format_to(out, " /TRANSFORM:CALLS");
	}
	if (m_truncateFloats) {
		format_to(out, " /TRUNCATEFLOATS:{}", m_truncateBits);
	}
	if (m_overrideAlignments) {
		format_to(out, " /OVERRIDEALIGNMENTS");
		if (m_alignmentBits != -1) {
			format_to(out, ":{}", m_alignmentBits);
		}
	}
	if (m_unalignCode) {
		format_to(out, " /UNALIGNCODE");
	}
	if (!m_runInitializers) {
		format_to(out, " /NOINITIALIZERS");
	}
	for (const Export& e : m_exports) {
		if (e.HasValue()) {
			format_to(out, " /EXPORT:{}=0x{:08X}", e.GetName(), e.GetValue());
		} else if (e.GetName() == e.GetSymbol()) {
			format_to(out, " /EXPORT:{}", e.GetName());
		} else {
			format_to(out, " /EXPORT:{}={}", e.GetName(), e.GetSymbol());
		}
	}
}

void Crinkler::InitProgressBar() {
	m_progressBar.AddProgressBar(&m_consoleBar);
	if(m_showProgressBar)
		m_progressBar.AddProgressBar(&m_windowBar);
	m_progressBar.Init();
}

void Crinkler::DeinitProgressBar() {
	m_progressBar.Deinit();
}