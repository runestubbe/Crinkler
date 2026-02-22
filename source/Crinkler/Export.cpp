#include "Export.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Crinkler.h"

#include <set>
#include <algorithm>

Export::Export(const std::string name, const std::string symbol)
	: m_name(std::move(name)), m_symbol(std::move(symbol)), m_value(0)
{}

Export::Export(const std::string name, int value)
	: m_name(std::move(name)), m_symbol(""), m_value(value)
{}

Export ParseExport(const std::string& name, const std::string& value) {
	if (value.empty()) {
		return Export(name, name);
	}
	
	char* end;
	long long v = strtoll(value.c_str(), &end, 0);
	if (end != value.c_str() && *end == 0) {
		return Export(name, (int)v);
	}
	return Export(name, value);
}

// Exports must be sorted by name
Hunk* CreateExportTable(const std::set<Export>& exports) {
	// Collect export values and sum name lengths
	std::map<int, int> values;
	int total_name_length = 0;
	for (const Export& e : exports) {
		if (e.HasValue()) {
			values[e.GetValue()] = 0;
		}
		total_name_length += (int)e.GetName().length() + 1;
	}

	// Space for hunk
	int table_offset = (int)values.size() * 4;
	int addresses_offset = table_offset + 40;
	int name_pointers_offset = addresses_offset + (int)exports.size() * 4;
	int ordinals_offset = name_pointers_offset + (int)exports.size() * 4;
	int names_offset = ordinals_offset + (int)exports.size() * 2;
	int hunk_size = names_offset + total_name_length;
	std::vector<char> data(hunk_size);
	int* words = (int*)&data[0];

	// Put values
	int index = 0;
	for (auto& v : values) {
		*words++ = v.first;
		v.second = index++;
	}
	assert((char*)words == &data[table_offset]);

	// Put table
	*words++ = 0;					// Flags
	*words++ = 0;					// Timestamp
	*words++ = 0;					// Major/Minor version
	*words++ = 0;					// Name rva
	*words++ = 1;					// Ordinal base
	*words++ = (int)exports.size();	// Address table entries
	*words++ = (int)exports.size();	// Number of name pointers
	*words++ = -CRINKLER_IMAGEBASE;	// Export address table rva
	*words++ = -CRINKLER_IMAGEBASE;	// Name pointer rva
	*words++ = -CRINKLER_IMAGEBASE;	// Ordinal table rva
	assert((char*)words == &data[addresses_offset]);

	// Put addresses and name pointers
	for (unsigned i = 0; i < exports.size() * 2; i++) {
		*words++ = -CRINKLER_IMAGEBASE;
	}
	assert((char*)words == &data[ordinals_offset]);

	// Put ordinals
	short* ordinals = (short*)words;
	for (unsigned i = 0; i < exports.size(); i++) {
		*ordinals++ = i;
	}
	assert((char*)ordinals == &data[names_offset]);

	// Put names
	char* names = (char*)ordinals;
	for (const Export& e : exports) {
		const char* name = e.GetName().c_str();
		strcpy(names, name);
		names += strlen(name) + 1;
	}
	assert(names == &data[0] + hunk_size);

	// Create hunk
	Hunk* hunk = new Hunk("Exports", &data[0], HUNK_IS_EXPORT | HUNK_IS_TRAILING, 2, hunk_size, hunk_size);
	std::string object_name = "EXPORT";

	// Add labels
	hunk->AddSymbol(new Symbol("exports", 0, SYMBOL_IS_RELOCATEABLE | SYMBOL_IS_SECTION, hunk, object_name.c_str()));
	for (const Export& e : exports) {
		if (e.HasValue()) {
			hunk->AddSymbol(new Symbol(e.GetName().c_str(), values[e.GetValue()] * 4, SYMBOL_IS_RELOCATEABLE, hunk));
		}
	}
	hunk->AddSymbol(new Symbol("_ExportTable", table_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	hunk->AddSymbol(new Symbol("_ExportAddresses", addresses_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	hunk->AddSymbol(new Symbol("_ExportNames", name_pointers_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	hunk->AddSymbol(new Symbol("_ExportOrdinals", ordinals_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	int name_offset = names_offset;
	for (const Export& e : exports) {
		std::string name_label = "_ExportName_" + e.GetName();
		hunk->AddSymbol(new Symbol(name_label.c_str(), name_offset, SYMBOL_IS_RELOCATEABLE, hunk));
		name_offset += (int)e.GetName().length() + 1;
	}

	// Add relocations
	hunk->AddRelocation({ "_ExportAddresses", table_offset + 28, RELOCTYPE_ABS32, object_name });
	hunk->AddRelocation({ "_ExportNames", table_offset + 32, RELOCTYPE_ABS32, object_name });
	hunk->AddRelocation({ "_ExportOrdinals", table_offset + 36, RELOCTYPE_ABS32, object_name });
	int i = 0;
	for (const Export& e : exports) {
		const std::string& export_label = e.HasValue() ? e.GetName() : e.GetSymbol();
		hunk->AddRelocation({ export_label, addresses_offset + i * 4, RELOCTYPE_ABS32, object_name });
		std::string name_label = "_ExportName_" + e.GetName();
		hunk->AddRelocation({ name_label, name_pointers_offset + i * 4, RELOCTYPE_ABS32, object_name });
		i++;
	}

	return hunk;
}

std::set<Export> StripExports(Hunk* phase1, int exports_rva) {
	const int rva_to_offset = CRINKLER_IMAGEBASE - CRINKLER_CODEBASE;
	phase1->AppendZeroes(1); // To make sure names are terminated
	unsigned char* data = phase1->GetPtr();

	// Locate tables
	int table_offset = exports_rva + rva_to_offset;
	int* table = (int*)&data[table_offset];
	int n_exports = table[6];
	int addresses_offset = table[7] + rva_to_offset;
	int* addresses = (int*)&data[addresses_offset];
	int name_pointers_offset = table[8] + rva_to_offset;
	int* name_pointers = (int*)&data[name_pointers_offset];
	int ordinals_offset = table[9] + rva_to_offset;
	short* ordinals = (short*)&data[ordinals_offset];

	// Collect exports
	std::vector<std::pair<char*, int>> export_offsets;
	for (int i = 0; i < n_exports; i++) {
		int address_offset = addresses[ordinals[i]] + rva_to_offset;
		int name_offset = name_pointers[i] + rva_to_offset;
		char* name = (char*)&data[name_offset];
		export_offsets.emplace_back(name, address_offset);
	}
	std::stable_sort(export_offsets.begin(), export_offsets.end(), [](const std::pair<char*, int>& a, const std::pair<char*, int>& b) {
		return a.second < b.second;
	});

	std::set<Export> exports;

	// Extract value exports
	int export_hunk_offset = table_offset;
	while (export_offsets.size() > 0 && export_offsets.back().second >= export_hunk_offset - 4) {
		int value = *(int*)&data[export_offsets.back().second];
		exports.insert(Export(export_offsets.back().first, value));
		export_hunk_offset = export_offsets.back().second;
		export_offsets.pop_back();
	}

	// Get remaining exports
	for (auto& export_offset : export_offsets) {
		char* name = export_offset.first;
		int offset = export_offset.second;
		phase1->AddSymbol(new Symbol(name, offset, SYMBOL_IS_RELOCATEABLE, phase1, "EXPORT"));
		exports.insert(Export(name, name));
	}

	// Truncate hunk
	phase1->SetRawSize(export_hunk_offset);

	return exports;
}

void PrintExports(const std::set<Export>& exports) {
	for (const Export& e : exports) {
		if (e.HasValue()) {
			printf("  %s = 0x%08X\n", e.GetName().c_str(), e.GetValue());
		}
		else if (e.GetSymbol() == e.GetName()) {
			printf("  %s\n", e.GetName().c_str());
		}
		else {
			printf("  %s -> %s\n", e.GetName().c_str(), e.GetSymbol().c_str());
		}
	}
}
