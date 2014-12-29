#include "HtmlReport.h"
#include "NameMangling.h"

#include <cstdio>
#include <algorithm>
#include <sstream>
#include <time.h>

#include "Compressor/Compressor.h"
#include "Log.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Crinkler.h"
#include "StringMisc.h"
#include "distorm.h"

using namespace std;

static const char* htmlHeader1 =
						"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">"
						"<html><head>"
						"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">"
						"<title>Crinkler compression report</title>"
						"<script type='text/javascript'>"
						"function collapse(el){"
							"el.style.display='none';"
							"el=el.parentNode.parentNode.previousSibling;"
							"while(el.className!='c1'){"
								"el=el.firstChild;"
							"}"
							"var address=el.innerHTML.substr(1);"
							"el.innerHTML='+'+address;"
						"}"
						"function expand(el){"
							"el.style.display='';"	
							"el=el.parentNode.parentNode.previousSibling;"
							"while(el.className!='c1'){"
								"el=el.firstChild;"
							"}"
							"var address=el.innerHTML.substr(1);"
							"el.innerHTML='-'+address;"
						"}"
						"function hideSections(){"
							"var el;"
							"for(i=0;(el=document.getElementById('h_'+i))!=null;i++){"
								"el.style.display = 'none';"
							"}"
							"expandPrefix(\"o_\");"
						"}"
						"function showSections(){"
							"var el;"
							"for(i=0;(el=document.getElementById('h_'+i))!=null;i++){"
								"el.style.display='';"
							"}"
						"}"
						"function switchMenu(obj){"
							"var el=document.getElementById(obj);"
							"if(el.style.display != 'none'){"
								"collapse(el);"
							"}else{"
								"expand(el);"
							"}"
						"}"
						"function collapsePrefix(prefix){"
							"var el;"
							"for(i=0;(el=document.getElementById(prefix+i))!=null;i++){"
								"collapse(el);"
							"}"
						"}"
						"function expandPrefix(prefix){"
							"var el;"
							"for(i=0;(el=document.getElementById(prefix+i))!=null;i++){"
								"expand(el);"
							"}"
						"}"
						"function collapseAll(){"
							"collapsePrefix('s_');"
							"collapsePrefix('o_');"
							"collapsePrefix('p_');"
							"showSections();"
						"}"
						"function expandAll(){"
							"expandPrefix('s_');"
							"expandPrefix('o_');"
							"expandPrefix('p_');"
							"hideSections();"
						"}"
						"function collapseSections(){"
							"collapsePrefix(\"o_\");"
							"showSections();"
						"}"
						"function expandSections(){"
							"expandPrefix(\"o_\");"
							"showSections();"
						"}"
						"function startState(){"
							"collapsePrefix('o_');"
							"collapsePrefix('p_');"
						"}"
						"function recursiveExpand(id){"
							"var el=document.getElementById(id);"
							"while(el!=null){"
								"if(el.localName == 'DIV' && el.id.match('h_') == null){"
									"expand(el);"
								"}"
								"el=el.parentNode;"
							"}"
						"}"
						"</script>"
						"<style type='text/css'>"	//css style courtesy of gargaj :)
							"body{"
							"font-family:monospace;"
						"}"

						".data{"
							"border-collapse:collapse;"
						"}"
						".data td{"
							"border:0px;"
						"}"
						".grptable{"
							"border-collapse:collapse;"
							"border:0px;"
						"}"
						".grptable td{"
							"border:0px;"
							"padding:0px;"
						"}"
						".grptable th{"
							"border:0px;"
							"padding:0px;"
						"}"
						".address{width:7em;text-align:left;white-space:nowrap;}"
						".private_symbol_row th{"
							"background-color:#eef;"
						"}"
						".public_symbol_row th{"
							"background-color:#99c;"
							"font-weight:bold;"
						"}"
						".oldsection_symbol_row th{"
							"background-color:#c88;"
							"font-weight:bold;"
						"}"
						".section_symbol_row th{"
							"background-color:#8c8;"
							"font-weight:bold;"
						"}"

						".public_symbol_row_expandable th{"
							"cursor: pointer;"
							"cursor: hand;"
							"background-color:#99c;"
							"font-weight:bold;"
						"}"
						".oldsection_symbol_row_expandable th{"
							"cursor: pointer;"
							"cursor: hand;"
							"background-color:#c88;"
							"font-weight:bold;"
						"}"
						".section_symbol_row_expandable th{"
							"cursor: pointer;"
							"cursor: hand;"
							"background-color:#8c8;"
							"font-weight:bold;"
						"}"
						".c1{width:7em;text-align:left;white-space:nowrap;}"
						".c2{width:40em;text-align:right;white-space:nowrap;}"
						".c3{width:7em;text-align:right;white-space:nowrap;}"
						".c4{width:7em;text-align:right;white-space:nowrap;}"
						".c5{width:7em;text-align:right;white-space:nowrap;}"
						"a:link { color: #333333; }"
						"a:visited { color: #333333; }"
						"a:hover { color: #333333; }"
						"a:active { color: #333333; }"
						"</style>"
						"</head><body onload='startState()'>"
						//header
						"<h1>Crinkler compression report</h1>"
						"<p><b>Report for file %s generated by " CRINKLER_WITH_VERSION " on %s</b></p>";

static const char* htmlHeader2 = 
						"<p><b>Click on a label to expand or collapse its contents.</b></p>"
						"<a href='#' onclick='collapseAll()'>collapse all</a>&nbsp;"
						"<a href='#' onclick='expandAll()'>expand all</a>&nbsp;"
						"<a href='#' onclick='collapseSections()'>collapse sections</a>&nbsp;"
						"<a href='#' onclick='expandSections()'>expand sections</a>&nbsp;"
						"<a href='#' onclick='hideSections()'>hide sections</a>&nbsp;"
						"<a href='#' onclick='collapsePrefix(\"p_\")'>collapse globals</a>&nbsp;"
						"<a href='#' onclick='expandPrefix(\"p_\")'>expand globals</a>&nbsp;"
						"<table class='grptable'><tr>"
						"<th nowrap class='c1'>&nbsp;Address</th>"
						"<th nowrap class='c2'>Label name</th>"
						"<th nowrap class='c3'>Size</th>"
						"<th nowrap class='c4'>Comp. size</th>"
						"<th nowrap class='c5'>Ratio</th>"
						"</tr>";

static const char* htmlFooter = 
						"</table>"
						"<p><a href='http://crinkler.net'>http://www.crinkler.net</a></p>"
						"</body></html>";

const int NUM_COLUMNS = 16;
const int HEX_WIDTH = 38;
const int OPCODE_WIDTH = 12;
const int LABEL_COLOR = 0x808080;

static map<string, string> identmap;
static int num_divs[4];
static int num_sections;


struct scol {
	float size;
	unsigned color;
} sizecols[] = {
	{ 0.1f, 0x80ff80 },
	{ 0.5f, 0x00ff00 },
	{ 1.0f, 0x00c000 },
	{ 2.0f, 0x008040 },
	{ 3.0f, 0x006090 },
	{ 5.0f, 0x0020f0 },
	{ 7.0f, 0x0000a0 },
	{ 9.0f, 0x000000 },
	{ 12.0f, 0x900000 },
	{ 1000000.0f, 0xff0000 },
};

//converts a string to an unique identifier consisting of only ['A'-Z']

static string toIdent(string str) {
	static char buff[16] = {'A', 0};
	if(identmap[str] != "") {
		return identmap[str];
	}
	string ident = buff;
	identmap[str] = ident;

	buff[0]++;
	int i = 0;
	while(buff[i] > 'Z') {
		buff[i++] = 'A';
		buff[i] = buff[i] ? buff[i]+1 : 'A';
	}

	return ident;
}

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
	return (c > 32 && c <= 126 || c >= 160) ? c : '.';
}

static int printRow(FILE *out, Hunk& hunk, const int *sizefill, int index, int n, bool ascii, bool spacing) {
	int width = 0;
	for(int x = 0; x < n; x++) {
		unsigned char c = 0;
		int size = 0;
		int idx = index + x;
		if(idx < hunk.getRawSize()) {
			size = sizefill[idx+1]-sizefill[idx];
			c = hunk.getPtr()[idx++];
		}

		if (spacing && x > 0 && (x&3) == 0)
		{
			fprintf(out,"<td>&nbsp;</td>");
			width += 1;
		}

		if (ascii) {
			fprintf(out,"<td title='%.2f bits' style='color: #%.6X;'>"
				"&#x%.2X;</td>", size / (float)BITPREC, sizeToColor(size), toAscii(c));
			width += 1;
		} else {
			fprintf(out,"<td title='%.2f bits' style='color: #%.6X;'>"
				"%.2X</td>", size / (float)BITPREC, sizeToColor(size), c);
			width += 2;
		}
	}
	return width;
}

//return the relocation symbol offset bytes into the instruction. Handles off bounds reads gracefully
static Symbol* getRelocationSymbol(_DecodedInst& inst, int offset, map<int, Symbol*>& relocs) {
	if(offset < 0 || offset > (int)inst.size-4)
		return NULL;
	offset += (int)inst.offset-CRINKLER_CODEBASE;

	map<int, Symbol*>::iterator it = relocs.find(offset);
	return it != relocs.end() ? it->second : NULL;
}

static std::string generateLabel(Symbol* symbol, int value, map<int, Symbol*>& symbols) {
	char buff[1024];
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
	string ident = toIdent(symbol->name);
	name = "<a href='#" + ident + "' onclick='recursiveExpand(\"" + ident + "\")'>" + name + "</a>";	//add link
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
	int value;
	if(sscanf_s(operands.c_str(), "%X", &value)) {
		map<int, Symbol*>::iterator it = symbols.find(value-CRINKLER_CODEBASE);
		if(it != symbols.end()) {
			operands = generateLabel(it->second, value, symbols);
		}
	}

	return operands;
}

string hashname(const char* name) {
	static map<string, int> namemap;
	static int counter = 1;
	if(namemap[name] == 0) {
		namemap[name] = counter++;
	}
	char buff[128];
	sprintf_s(buff, sizeof(buff), "v%d", namemap[name]);
	return buff;
}

//Sorts pair<string, int> by greater<int> and then by name
bool opcodeComparator(const pair<string, int>& a, const pair<string, int>& b) {
	if(a.second != b.second)
		return a.second > b.second;
	else
		return a.first < b.first;
}

static void htmlReportRecursive(CompressionReportRecord* csr, FILE* out, Hunk& hunk, Hunk& untransformedHunk, const int* sizefill, bool iscode, map<int, Symbol*>& relocs, map<int, Symbol*>& symbols, map<string, int>& opcodeCounters, const char *exefilename, Crinkler *crinkler) {
	string divstr;
	//handle enter node events
	if(csr->type & RECORD_ROOT) {
		//write header
		time_t rawtime;
		struct tm * timeinfo;
		time ( &rawtime );
		timeinfo = localtime ( &rawtime );
		fprintf(out, htmlHeader1, exefilename, asctime(timeinfo));

		// Print option string
		fprintf(out, "<p><b>Options:");
		crinkler->printOptions(out);
		fprintf(out, "</b></p>");

		fprintf(out, "<h3>Compression rate color codes:</h3><table>");
		int ncols = sizeof(sizecols)/sizeof(scol);
		for (int i = 0 ; i < ncols ; i++) {
			fprintf(out, "<tr><td bgcolor='#%.6X'>&nbsp;&nbsp;</td><td>", sizecols[i].color);
			if (i == 0) {
				fprintf(out, "&nbsp;Less than %1.1f bits per byte", sizecols[i].size);
			} else if (i == ncols-1) {
				fprintf(out, "&nbsp;More than %1.1f bits per byte", sizecols[i-1].size);
			} else {
				fprintf(out, "&nbsp;Between %1.1f and %1.1f bits per byte", sizecols[i-1].size, sizecols[i].size);
			}
			fprintf(out, "</td></tr>\n");
		}
		fprintf(out, "</table>\n");

		fprintf(out, htmlHeader2);
	} else {
		if(csr->type & RECORD_CODE) {
			iscode = true;
		}

		string label;
		string css_class;
		string script;
		char div_prefix = 0;
		int level = csr->getLevel();
		switch(level) {
			case 0:	//section
				label = csr->name;
				css_class = "section_symbol_row";
				div_prefix = 's';
				break;
			case 1:	//old section
				label = stripPath(csr->miscString) + ":" + stripCrinklerSymbolPrefix(csr->name.c_str());
				css_class = "oldsection_symbol_row";
				div_prefix  = 'o';
				break;
			case 2:	//public symbol
				label = stripCrinklerSymbolPrefix(csr->name.c_str());
				css_class = "public_symbol_row";
				div_prefix = 'p';
				break;
			case 3:	//private symbol
				label = stripCrinklerSymbolPrefix(csr->name.c_str());
				css_class = "private_symbol_row";
				break;
		}

		if(level == 1) {	//old section
			fprintf(out, "<tr><td nowrap colspan='5'><div id='h_%d'><table class='grptable'>", num_sections++);
		}
		if(csr->children.empty() && (csr->size == 0 || csr->compressedPos < 0))
			div_prefix = 0;
		if(div_prefix) {
			stringstream ss;
			ss << div_prefix << "_" << num_divs[level]++;
			divstr = ss.str();
			fprintf(out, "<tr class='%s_expandable' onclick=\"switchMenu('%s');\">", css_class.c_str(), divstr.c_str());
		} else {
			fprintf(out, "<tr class='%s'>", css_class.c_str());
		}

		//make the label an anchor
		if(!(csr->type & RECORD_DUMMY)) {	//don't put anchors on dummy records
			label = "<a id='" + toIdent(csr->name) + "'>" + label + "</a>";
		}
		
		fprintf(out,"<th nowrap class='c1'>%s%.8X&nbsp;</th>"
					"<th nowrap class='c2'>%s</th>", divstr.empty() ? "&nbsp;" : "-", CRINKLER_CODEBASE + csr->pos, label.c_str());

		
		if(csr->size > 0) {
			fprintf(out, "<th nowrap class='c3'>%d</th>", csr->size);
			if(csr->compressedPos >= 0) {	//initialized data
				fprintf(out,"<th nowrap class='c4'>%.2f</th>"
							"<th nowrap class='c5'>%.1f%%</th>", 
							csr->compressedSize / (BITPREC*8.0f),
							(csr->compressedSize / (BITPREC*8.0f)) * 100 / csr->size);
			} else {	//uninitialized data
				fprintf(out,"<th nowrap class='c4'>&nbsp;</th>"
							"<th nowrap class='c5'>&nbsp;</th>");
			}
		} else {				//duplicate label
			fprintf(out,"<th nowrap class='c3'>&nbsp;</th>"
						"<th nowrap class='c4'>&nbsp;</th>"
						"<th nowrap class='c5'>&nbsp;</th>");
		}
		fprintf(out,"</tr>");
		if(level == 1) {	//old section
			fprintf(out, "</table></div></td></tr>");
		}


		if(!divstr.empty())
			fprintf(out,"<tr><td nowrap colspan='5'><div id='%s'><table class='grptable'>", divstr.c_str());
		else
			fprintf(out,"<tr><td nowrap colspan='5'><table class='grptable'>");

		if(csr->children.empty() || csr->pos < csr->children.front()->pos) {	//
			int size = csr->children.empty() ? csr->size : csr->children.front()->pos - csr->pos;
			
			if(csr->compressedPos >= 0 && size > 0) {
				if (iscode) {
					// Code section

					// Allocate space for worst-case number of instructions
					int instspace = size;
					if (instspace < 15) instspace = 15;
					_DecodedInst *insts = (_DecodedInst *)malloc(instspace * sizeof(_DecodedInst));
					unsigned int numinsts;
					distorm_decode64(CRINKLER_CODEBASE + csr->pos, (unsigned char*)&untransformedHunk.getPtr()[csr->pos], size, Decode32Bits, insts, instspace, &numinsts);
					for(int i = 0; i < (int)numinsts; i++) {
						fprintf(out, "<tr>");
						//address
						fprintf(out, "<td nowrap class='address'>&nbsp;%.8X&nbsp;</td>", insts[i].offset);
						
						//hexdump
						fprintf(out, "<td nowrap colspan=4 class='hexdump'><table class='data'><tr>");
						int hexsize = printRow(out, hunk, sizefill, (int)insts[i].offset - CRINKLER_CODEBASE, insts[i].size, false, false);
						fprintf(out, "<td>");
						for (int j = 0 ; j < HEX_WIDTH-hexsize ; j++) {
							fprintf(out, "&nbsp;");
						}
						fprintf(out, "</td>");

						//disassembly
						// Make hex digits uppercase
						for (unsigned char *cp = insts[i].operands.p ; *cp != 0 ; cp++) {
							if (*cp >= 'a' && *cp <= 'f') *cp += 'A'-'a';
						}
						int size = instructionSize(&insts[i], sizefill);

						fprintf(out, "<td nowrap title='%.2f bytes' style='color: #%.6X;'>%s",
							size / (float)(BITPREC*8), sizeToColor(size/insts[i].size), insts[i].mnemonic.p);
						for (int j = 0 ; j < OPCODE_WIDTH-(int)insts[i].mnemonic.length ; j++) {
							fprintf(out, "&nbsp;");
						}

						{
							string ops = calculateInstructionOperands(insts[i], untransformedHunk, relocs, symbols);
							const char *optext = ops.c_str();
							bool comma = false;
							while (*optext)
							{
								if (comma && *optext == ' ')
								{
									fprintf(out, "&nbsp;");
								} else {
									fputc(*optext, out);
								}
								comma = *optext == ',';
								optext++;
							}
						}
						fprintf(out, "</td></tr></table></tr>");
					}
					free(insts);
				} else {
					// Data section
					vector<int> rowLengths;
					{	//fill row lengths
						int count = 0;
						int idx = csr->pos;
						while(idx < csr->pos+size) {
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

					int idx = csr->pos;
					for(vector<int>::iterator it = rowLengths.begin(); it != rowLengths.end(); it++) {
						//print address
						fprintf(out, "<tr><td nowrap class='address'>&nbsp;%.8X&nbsp;</td>", CRINKLER_CODEBASE + idx);

						//hexdump
						fprintf(out, "<td nowrap colspan=4 class='hexdump'><table class='data'><tr>");
						int hexsize = printRow(out, hunk, sizefill, idx, *it, false, true);
						fprintf(out, "<td>");
						for (int j = 0 ; j < HEX_WIDTH-hexsize ; j++) {
							fprintf(out, "&nbsp;");
						}
						fprintf(out, "</td>");

						map<int, Symbol*>::iterator jt = relocs.find(idx);
						if(jt != relocs.end()) {
							//write label
							int size = sizefill[idx+4]-sizefill[idx];
							int value = *((int*)&untransformedHunk.getPtr()[idx]);
							string label = generateLabel(jt->second, value, symbols);
							fprintf(out, "<td title='%.2f bytes' style='color: #%.6X;' colspan='%d'>%s</td>",
								size / (float)(BITPREC*8), sizeToColor(size/4), NUM_COLUMNS, label.c_str());
						} else {
							//write ascii
							printRow(out, hunk, sizefill, idx, *it, true, false);
						}
						fprintf(out, "</tr></table></td></tr>");

						idx += *it;
					}
				}
			}
		}
	}

	for(vector<CompressionReportRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++)
		htmlReportRecursive(*it, out, hunk, untransformedHunk, sizefill, iscode, relocs, symbols, opcodeCounters, exefilename, crinkler);

	//handle leave node event
	if(csr->type & RECORD_ROOT) {
		fprintf(out,htmlFooter);
	} else {
		fprintf(out,"</table>%s</td></tr>", divstr.empty() ? "" : "</div>");
	}
}

void htmlReport(CompressionReportRecord* csr, const char* filename, Hunk& hunk, Hunk& untransformedHunk, const int* sizefill, const char *exefilename, Crinkler *crinkler) {
	identmap.clear();
	num_divs[0] = num_divs[1] = num_divs[2] = num_divs[3] = 0;
	num_sections = 0;
	
	map<int, Symbol*> relocs = hunk.getOffsetToRelocationMap();
	map<int, Symbol*> symbols = hunk.getOffsetToSymbolMap();
	FILE* out;
	map<string, int> opcodeCounters;
	if(fopen_s(&out, filename, "wb")) {
		Log::error("filename", "Cannot open file for writing");
		return;
	}
	htmlReportRecursive(csr, out, hunk, untransformedHunk, sizefill, false, relocs, symbols, opcodeCounters, exefilename, crinkler);

	fclose(out);
}
