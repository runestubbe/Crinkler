#include "Export.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Crinkler.h"

#include <set>

Export::Export(const std::string name, const std::string symbol)
	: m_name(std::move(name)), m_symbol(std::move(symbol)), m_value(0)
{}

Export::Export(const std::string name, int value)
	: m_name(std::move(name)), m_symbol(""), m_value(value)
{}

Export parseExport(const std::string& name, const std::string& value) {
	if (value.empty()) {
		return Export(name, name);
	}
	if (value[0] >= '0' && value[0] <= '9') {
		int v;
		char *end;
		if (value[0] == '0') {
			if (value.length() > 2 && (value[1] == 'x' || value[1] == 'X')) {
				v = strtol(&value[2], &end, 16);
			} else {
				v = strtol(&value[0], &end, 8);
			}
		} else {
			v = strtol(&value[0], &end, 10);
		}
		if (end != &value[value.length()]) {
			fprintf(stderr, "error: illegal numeric value for export %s: %s\n", name.c_str(), value.c_str());
			exit(1);
		}
		return Export(name, v);
	}
	return Export(name, value);
}

// Exports must be sorted by name
Hunk* createExportTable(const std::set<Export>& exports) {
	// Collect export values and sum name lengths
	std::map<int, int> values;
	int total_name_length = 0;
	for (const Export& e : exports) {
		if (e.hasValue()) {
			values[e.getValue()] = 0;
		}
		total_name_length += e.getName().length() + 1;
	}

	// Space for hunk
	int table_offset = values.size() * 4;
	int addresses_offset = table_offset + 40;
	int name_pointers_offset = addresses_offset + exports.size() * 4;
	int ordinals_offset = name_pointers_offset + exports.size() * 4;
	int names_offset = ordinals_offset + exports.size() * 2;
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
	*words++ = 0;					// flags
	*words++ = 0;					// timestamp
	*words++ = 0;					// major/minor version
	*words++ = 0;					// name rva
	*words++ = 1;					// ordinal base
	*words++ = exports.size();		// address table entries
	*words++ = exports.size();		// number of name pointers
	*words++ = -CRINKLER_IMAGEBASE;	// export address table rva
	*words++ = -CRINKLER_IMAGEBASE;	// name pointer rva
	*words++ = -CRINKLER_IMAGEBASE;	// ordinal table rva
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
		const char* name = e.getName().c_str();
		strcpy(names, name);
		names += strlen(name) + 1;
	}
	assert(names == &data[hunk_size]);

	// Create hunk
	Hunk* hunk = new Hunk("Exports", &data[0], HUNK_IS_TRAILING, 2, hunk_size, hunk_size);
	std::string object_name = "EXPORT";

	// Add labels
	hunk->addSymbol(new Symbol("exports", 0, SYMBOL_IS_RELOCATEABLE | SYMBOL_IS_SECTION, hunk, object_name.c_str()));
	for (const Export& e : exports) {
		if (e.hasValue()) {
			hunk->addSymbol(new Symbol(e.getName().c_str(), values[e.getValue()] * 4, SYMBOL_IS_RELOCATEABLE, hunk));
		}
	}
	hunk->addSymbol(new Symbol("_ExportTable", table_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	hunk->addSymbol(new Symbol("_ExportAddresses", addresses_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	hunk->addSymbol(new Symbol("_ExportNames", name_pointers_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	hunk->addSymbol(new Symbol("_ExportOrdinals", ordinals_offset, SYMBOL_IS_RELOCATEABLE, hunk));
	int name_offset = names_offset;
	for (const Export& e : exports) {
		std::string name_label = "_ExportName_" + e.getName();
		hunk->addSymbol(new Symbol(name_label.c_str(), name_offset, SYMBOL_IS_RELOCATEABLE, hunk));
		name_offset += e.getName().length() + 1;
	}

	// Add relocations
	hunk->addRelocation({ "_ExportAddresses", table_offset + 28, RELOCTYPE_ABS32, object_name });
	hunk->addRelocation({ "_ExportNames", table_offset + 32, RELOCTYPE_ABS32, object_name });
	hunk->addRelocation({ "_ExportOrdinals", table_offset + 36, RELOCTYPE_ABS32, object_name });
	int i = 0;
	for (const Export& e : exports) {
		const std::string& export_label = e.hasValue() ? e.getName() : e.getSymbol();
		hunk->addRelocation({ export_label, addresses_offset + i * 4, RELOCTYPE_ABS32, object_name });
		std::string name_label = "_ExportName_" + e.getName();
		hunk->addRelocation({ name_label, name_pointers_offset + i * 4, RELOCTYPE_ABS32, object_name });
		i++;
	}

	return hunk;
}

std::vector<Export> stripExports(Hunk* phase1) {
	std::vector<Export> exports;

	// TODO

	return exports;
}
