#include "HtmlSummary.h"
#include "NameMangling.h"

#include <cstdio>
#include <algorithm>

#include "Compressor/Compressor.h"
#include "Log.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Crinkler.h"
#include "StringMisc.h"
#include "distorm.h"

using namespace std;

const int NUM_COLUMNS = 16;
const int OPCODE_WIDTH = 11;
const int LABEL_COLOR = 0x808080;

struct scol {
	float size;
	unsigned color;
} sizecols[] = {
	{ 0.1f, 0x80ff80 },
	{ 0.5f, 0x00ff00 },
	{ 1.0f, 0x00b000 },
	{ 2.0f, 0x008040 },
	{ 3.0f, 0x006090 },
	{ 5.0f, 0x0020f0 },
	{ 7.0f, 0x0000a0 },
	{ 9.0f, 0x000000 },
	{ 12.0f, 0x900000 },
};

static int sizeToColor(int size) {
	float bsize = size / (float)BITPREC;
	for (int i = 0 ; i < sizeof(sizecols)/sizeof(scol) ; i++) {
		if (bsize < sizecols[i].size) {
			return sizecols[i].color;
		}
	}
	return 0xff0000;
}

static int instructionSize(_DecodedInst *inst, const int *sizefill) {
	int idx = (int)inst->offset - CRINKLER_CODEBASE;
	return sizefill[idx + inst->size] - sizefill[idx];
}

static int toAscii(int c) {
	return (c >= 32 && c <= 126 || c >= 160) ? c : '.';
}

static void printRow(FILE *out, Hunk& hunk, const int *sizefill, int index, int n, bool ascii) {
	for(int x = 0; x < n; x++) {
		unsigned char c = 0;
		int size = 0;
		int idx = index + x;
		if(idx < hunk.getRawSize()) {
			size = sizefill[idx+1]-sizefill[idx];
			c = hunk.getPtr()[idx++];
		}

		if (ascii) {
			fprintf(out,"<td title=\"%.2f bits\" style=\"color: #%.6X;\">"
				"&#x%.2X;</td>", size / (float)BITPREC, sizeToColor(size), toAscii(c));
		} else {
			fprintf(out,"<td title=\"%.2f bits\" style=\"color: #%.6X;\">"
				"%.2X</td>", size / (float)BITPREC, sizeToColor(size), c);
		}
	}

}

//return the relocation symbol offset bytes into the instruction. Handles off bounds reads gracefully
static Symbol* getRelocationSymbol(_DecodedInst& inst, int offset, map<int, Symbol*>& relocs) {
	if(offset < 0 || offset > inst.size-4)
		return NULL;
	offset += inst.offset-CRINKLER_CODEBASE;
	
	map<int, Symbol*>::iterator it = relocs.find(offset);
	return it != relocs.end() ? it->second : NULL;
}

static std::string generateLabel(Symbol* symbol, int value, map<int, Symbol*>& symbols) {
	char buff[128];
	int offset = 0;
	if(symbol->flags & SYMBOL_IS_RELOCATEABLE) {
		offset = value - (symbol->value+CRINKLER_CODEBASE);
		
		//prefer label over label+offset, eventhough it makes us change symbol
		if(offset != 0) {
			map<int, Symbol*>::iterator it = symbols.find(value-CRINKLER_CODEBASE);
			if(it != symbols.end()) {
				symbol = it->second;
				offset = 0;
			}
		}
	}
	string name = stripCrinklerSymbolPrefix(symbol->name.c_str());
	if(offset > 0)
		sprintf_s(buff, sizeof(buff), "%s+0x%X", name.c_str(), offset);
	else if(offset < 0)
		sprintf_s(buff, sizeof(buff), "%s-0x%X", name.c_str(), -offset);
	else
		sprintf_s(buff, sizeof(buff), "%s", name.c_str());
	return buff;
}

//finds a hexnumber, removes it from the string and returns its position. value is set to the value of the number
static void extractNumber(string& str, int begin, int& value, string::size_type& startpos, string::size_type& endpos) {
	startpos = str.find("0x", begin);
	if(startpos == string::npos)
		return;

	endpos = startpos+2;
	while(endpos < str.size() && 
		(str[endpos] >= '0' && str[endpos] <= '9' ||
		str[endpos] >= 'A' && str[endpos] <= 'F')) {
			endpos++;
	}
	
	sscanf_s(&str.c_str()[startpos], "%X", &value);
}

static string calculateInstructionOperands(_DecodedInst& inst, Hunk& hunk, map<int, Symbol*>& relocs, map<int, Symbol*> symbols) {
	string operands = (char*)inst.operands.p;

	//find number operands in textual assembly
	int number1_value = 0;
	string::size_type number1_startpos = string::npos, number1_endpos = string::npos;			//immediate or displace
	extractNumber(operands, 0, number1_value, number1_startpos, number1_endpos);
	int number2_value = 0;
	string::size_type number2_startpos = string::npos, number2_endpos = string::npos;			//displace
	if(number1_startpos != string::npos) {
		extractNumber(operands, number1_startpos+1, number2_value, number2_startpos, number2_endpos);
		if(number2_startpos != string::npos && operands[number1_endpos] == ']') {
			swap(number1_startpos, number2_startpos);
			swap(number1_endpos, number2_endpos);
			swap(number1_value, number2_value);
		}
	}

	//find relocation
	Symbol* reloc_symbol1 = getRelocationSymbol(inst, inst.size-4, relocs);//imm32 or disp32
	Symbol* reloc_symbol2 = NULL;	//disp32

	reloc_symbol2 = getRelocationSymbol(inst, inst.size-5, relocs);	//disp32 followed by imm8
	if(reloc_symbol2 == NULL)
		reloc_symbol2 = getRelocationSymbol(inst, inst.size-6, relocs);	//disp32 followed by imm16
	if(reloc_symbol2 == NULL)
		reloc_symbol2 = getRelocationSymbol(inst, inst.size-8, relocs);	//disp32 followed by imm32

	if(number1_startpos != string::npos && (reloc_symbol1 || reloc_symbol2)) {		//at least one number operand needs a fix
		if(number2_startpos == string::npos) {	//if number2 isn't there
			if(reloc_symbol1 == NULL)
				swap(reloc_symbol1, reloc_symbol2);	//force symbol1 into number1
		}

		int delta = 0;	//the amount label2 will need to be displaced
		if(reloc_symbol1) {
			string label = generateLabel(reloc_symbol1, number1_value, symbols);
			delta = label.size() - (number1_endpos-number1_startpos);
			operands.erase(number1_startpos, number1_endpos-number1_startpos);
			operands.insert(number1_startpos, label);
		}
		if(reloc_symbol2) {
			if(number2_startpos < number1_startpos)
				delta = 0;
			string label = generateLabel(reloc_symbol2, number2_value, symbols);
			operands.erase(number2_startpos+delta, number2_endpos-number2_startpos);
			operands.insert(number2_startpos+delta, label);
		}
	}

	//handle 8 and 16-bit displacements
	unsigned char* inst_ptr = (unsigned char*)&hunk.getPtr()[inst.offset-CRINKLER_CODEBASE];

	if((inst_ptr[0] >=0x70 && inst_ptr[0] < 0x80) ||		//jcc
		(inst_ptr[0] >= 0xE0 && inst_ptr[0] <= 0xE3) ||	//loop
		(inst_ptr[0] == 0xE8)) {						//jmp

		int value;
		sscanf_s(operands.c_str(), "%X", &value);
		map<int, Symbol*>::iterator it = symbols.find(value-CRINKLER_CODEBASE);
		if(it != symbols.end()) {
			operands = generateLabel(it->second, value, symbols);
		}
	}

	return operands;
}

static void htmlSummaryRecursive(CompressionSummaryRecord* csr, FILE* out, Hunk& hunk, const int* sizefill, bool iscode, map<int, Symbol*>& relocs, map<int, Symbol*>& symbols) {
	//handle enter node events
	if(csr->type & RECORD_ROOT) {
		//write header
		fprintf(out,
			"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">"
			"<html><head>"
			"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">"
			"<title>Crinkler compression summary</title>"
			"<style  type=\"text/css\">"	//css style courtesy of gargaj :)
			"body {"
				"font-family: verdana;"
			"}"
			".outertable {"
				"border-collapse: collapse;"
				"font-size: 11px;"
			"}"
			".outertable th {"
				"background: #eee;"
			"}"
			".outertable td {"
				"border: #eee 1px solid;"
			"}"
			".data {"
				"font-family: monospace;"
				"border-collapse: collapse;"
			"}"
			".data td {"
				"border: 0px;"
			"}"
			".fillcell {"
			"}"
			".private_symbol_row {"
				"background-color: #eef;"
			"}"
			".public_symbol_row {"
				"background-color: #99c;"
				"font-weight: bold;"
			"}"
			".section_symbol_row {"
				"background-color: #c88;"
				"font-weight: bold;"
			"}"
			"</style>"

			"</head><body>", LABEL_COLOR);
	} else {
		if(csr->type & RECORD_SECTION) {
			fprintf(out,
				"<h1>%s</h1>"
				"<table class=\"outertable\">"
				"<tr>"
				"<th>address</th>"
				"<th>label name</th>"
				"<th>size</th>",
				csr->name.c_str(),
				csr->name.c_str());

			if (csr->name.c_str()[0] == 'U') {
				fprintf(out, "</tr>");
			} else {
				fprintf(out,
					"<th>comp. size</th>"
					"<th>ratio</th>"
					"</tr>");
			}
			if (csr->name.c_str()[0] == 'C') {
				iscode = true;
			}
		} else {
			string label;
			if(csr->type & RECORD_OLD_SECTION) {
				label = stripPath(csr->miscString) + ":" + csr->name;
				fprintf(out, "<tr class=section_symbol_row>");
			} else {
				label = csr->name;
				fprintf(out, "<tr class=\"%s_symbol_row\">", (csr->type & RECORD_PUBLIC) ? "public" : "private");
			}
			
			fprintf(out, "<td><table class=\"data\"><tr><td class=\"address\">%.8X&nbsp;</td></tr></table></td>", CRINKLER_CODEBASE + csr->pos);
			
			

			int colspan = 1;
			if(csr->size == 0)
				if(csr->compressedPos >= 0)	//initialized data
					colspan = 4;
				else
					colspan = 2;

			
			fprintf(out, "<td colspan=\"%d\">%s</td>", colspan, label.c_str());

			if (csr->size > 0) {
				if(csr->compressedPos >= 0) {
					fprintf(out,
						"<td align=right>%d</td>"
						"<td align=right>%.2f</td>"
						"<td align=right>%.1f%%</td>",
						csr->size,
						csr->compressedSize / (BITPREC*8.0f),
						(csr->compressedSize / (BITPREC*8.0f)) * 100 / csr->size);
				} else {
					fprintf(out, "<td align=right>%d</td>", csr->size);
				}
			}
			fprintf(out,"</tr>");

			if(csr->compressedPos >= 0 && csr->size > 0) {
				if (iscode) {
					// Code section

					// Allocate space for worst-case number of instructions
					int instspace = csr->size;
					if (instspace < 15) instspace = 15;
					_DecodedInst *insts = (_DecodedInst *)malloc(instspace * sizeof(_DecodedInst));
					unsigned int numinsts;
					distorm_decode(CRINKLER_CODEBASE + csr->pos, (unsigned char*)&hunk.getPtr()[csr->pos], csr->size, Decode32Bits, insts, instspace, &numinsts);

					// Print instruction adresses
					fprintf(out, "<tr><td><table class=\"data\">");
					for (int i = 0 ; i < numinsts ; i++) {
						fprintf(out, "<tr>");
						fprintf(out, "<td class=\"address\">%.8X&nbsp;</td>", insts[i].offset);
						fprintf(out, "</tr>");
					}
					fprintf(out, "</table></td>");

					// Print hex dump of instructions
					fprintf(out, "<td><table class=\"data\">");
					for (int i = 0 ; i < numinsts ; i++) {
						fprintf(out, "<tr>");
						printRow(out, hunk, sizefill, (int)insts[i].offset - CRINKLER_CODEBASE, insts[i].size, false);
						fprintf(out, "</tr>");
					}
					fprintf(out, "</table></td>");

					// Print instructions
					fprintf(out, "<td colspan=3><table class=\"data\">");
					for (int i = 0 ; i < numinsts ; i++) {
						// Make hex digits uppercase
						for (unsigned char *cp = insts[i].operands.p ; *cp != 0 ; cp++) {
							if (*cp >= 'a' && *cp <= 'f') *cp += 'A'-'a';
						}
						int size = instructionSize(&insts[i], sizefill);

						fprintf(out, "<tr><td title=\"%.2f bytes\" style=\"color: #%.6X;\">%s",
							size / (float)(BITPREC*8), sizeToColor(size/insts[i].size), insts[i].mnemonic.p);
						for (int j = 0 ; j < OPCODE_WIDTH-insts[i].mnemonic.length ; j++) {
							fprintf(out, "&nbsp;");
						}
				
						fprintf(out, "&nbsp;%s</td>", calculateInstructionOperands(insts[i], hunk, relocs, symbols).c_str());
						fprintf(out, "</tr>");
						
					}
					fprintf(out, "</table></td></tr>");

					free(insts);

				} else {
					// Data section
					vector<int> rowLengths;
					{	//fill row lenghts
						int count = 0;
						int idx = csr->pos;
						while(idx < csr->pos+csr->size) {
							if(relocs.find(idx) == relocs.end()) {
								count++;
								if(count == NUM_COLUMNS) {
									rowLengths.push_back(NUM_COLUMNS);
									count = 0;
								}
								idx++;
							} else {
								if(count > 0)
									rowLengths.push_back(count);
								rowLengths.push_back(4);
								count = 0;
								idx+=4;
							}
						}
						if(count > 0)
							rowLengths.push_back(count);
					}

					// Print adresses
					{
						fprintf(out, "<tr><td><table class=\"data\">");
						int idx = csr->pos;
						for(vector<int>::iterator it = rowLengths.begin(); it != rowLengths.end(); it++) {
							fprintf(out, "<tr>");
							fprintf(out, "<td class=\"address\">%.8X&nbsp;</td>", CRINKLER_CODEBASE + idx);
							fprintf(out, "</tr>");
							idx += *it;
						}
						fprintf(out, "</table></td>");
					}

					//write hex
					{
						fprintf(out, "<td><table class=\"data\">");
						int idx = csr->pos;
						for(vector<int>::iterator it = rowLengths.begin(); it != rowLengths.end(); it++) {
							fprintf(out, "<tr>");
							printRow(out, hunk, sizefill, idx, *it, false);
							fprintf(out, "</tr>");
							idx += *it;	
						}
						fprintf(out, "</table></td>");
					}
					

					//write ascii
					{
						fprintf(out, "<td colspan=3><table class=\"data\">");
						int idx = csr->pos;
						for(vector<int>::iterator it = rowLengths.begin(); it != rowLengths.end(); it++) {
							fprintf(out, "<tr>");
							map<int, Symbol*>::iterator jt = relocs.find(idx);
							if(jt != relocs.end()) {
								//write label
								int size = sizefill[idx+4]-sizefill[idx];
								int value = *((int*)&hunk.getPtr()[idx]);
								string label = generateLabel(jt->second, value, symbols);
								fprintf(out, "<td title=\"%.2f bytes\" style=\"color: #%.6X;\" colspan=\"%d\">%s</td>",
									size / (float)(BITPREC*8), sizeToColor(size/4), NUM_COLUMNS, label.c_str());
							} else {
								//write ascii
								printRow(out, hunk, sizefill, idx, *it, true);
							}
							idx += *it;
						}
						fprintf(out, "</table></td></tr>");
					}
					
				}

			}
		}
	}

	for(vector<CompressionSummaryRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++)
		htmlSummaryRecursive(*it, out, hunk, sizefill, iscode, relocs, symbols);

	//handle leave node event
	if(csr->type == RECORD_ROOT) {
		fprintf(out, "</body></html>");
	} else {
		if(csr->type == RECORD_SECTION) {
			fprintf(out,"</table>");
		}
	}
}

void htmlSummary(CompressionSummaryRecord* csr, const char* filename, Hunk& hunk, const int* sizefill) {
	map<int, Symbol*> relocs = hunk.getOffsetToRelocationMap();
	map<int, Symbol*> symbols = hunk.getOffsetToSymbolMap();
	FILE* out;
	if(fopen_s(&out, filename, "wb")) {
		Log::error(0, "", "could not open '%s' for writing", filename);
		return;
	}
	htmlSummaryRecursive(csr, out, hunk, sizefill, false, relocs, symbols);

	fclose(out);
}
