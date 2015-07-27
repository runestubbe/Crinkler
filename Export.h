#pragma once

#include <string>
#include <vector>
#include <set>

class Hunk;
class Export {
	std::string m_name;
	std::string m_symbol;
	int m_value;

public:
	Export(const std::string name, const std::string symbol);
	Export(const std::string name, int value);

	const std::string& getName() const { return m_name; }
	const std::string& getSymbol() const { return m_symbol; }
	int getValue() const { return m_value; }

	bool hasValue() const { return m_symbol.empty(); }

	bool operator<(const Export& e) const { return m_name < e.m_name; }
};

Export parseExport(const std::string& name, const std::string& value);

Hunk* createExportTable(const std::set<Export>& exports);

std::set<Export> stripExports(Hunk* phase1, int exports_rva);

void printExports(const std::set<Export>& exports);
