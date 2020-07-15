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

	bool operator<(const Export& e) const { return m_name < e.m_name; }

	const std::string&	GetName() const { return m_name; }
	const std::string&	GetSymbol() const { return m_symbol; }
	int					GetValue() const { return m_value; }
	bool				HasValue() const { return m_symbol.empty(); }
};

Export ParseExport(const std::string& name, const std::string& value);

Hunk* CreateExportTable(const std::set<Export>& exports);

std::set<Export> StripExports(Hunk* phase1, int exports_rva);

void PrintExports(const std::set<Export>& exports);
