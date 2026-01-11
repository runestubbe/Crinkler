#include "HtmlReport.h"
#include "NameMangling.h"

#include <cstdio>
#include <algorithm>
#include <time.h>

#include "../Compressor/Compressor.h"
#include "Log.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Crinkler.h"
#include "StringMisc.h"
#include "../../external/distorm/distorm.h"

using namespace std;

static const char* htmlHeader0 = R""""(
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html><head>
<meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
<title>Crinkler compression report</title>
<script type='text/javascript'>
)"""";

static const char* script = R""""(
function collapse(el) {
	el.style.display = 'none';
	el = el.parentNode.parentNode.previousSibling;
	while (el.className != 'c1') {
		el = el.firstChild;
	}
	var address = el.innerHTML.substr(1);
	el.innerHTML = '+'+address;
}
function expand(el) {
	el.style.display = '';
	el = el.parentNode.parentNode.previousSibling;
	while (el.className != 'c1') {
		el = el.firstChild;
	}
	var address = el.innerHTML.substr(1);
	el.innerHTML = '-'+address;
}
function hideSections() {
	var el;
	for (i = 0; (el = document.getElementById('h_'+i)) != null; i++) {
		el.style.display  =  'none';
	}
	expandPrefix("o_");
}
function showSections() {
	var el;
	for (i = 0; (el = document.getElementById('h_'+i)) != null; i++) {
		el.style.display = '';
	}
}
function switchMenu(obj) {
	var el = document.getElementById(obj);
	if (el.style.display != 'none') {
		collapse(el);
	}else{
		expand(el);
	}
}
function collapsePrefix(prefix) {
	var el;
	for (i = 0; (el = document.getElementById(prefix+i)) != null; i++) {
		collapse(el);
	}
}
function expandPrefix(prefix) {
	var el;
	for (i = 0; (el = document.getElementById(prefix+i)) != null; i++) {
		expand(el);
	}
}
function markState(s) {
	for (i = 1; i <= 6; i++) {
		var e = document.getElementById('state_'+i);
		e.setAttribute('style', i == s ? 'font-weight:bold' : '');
	}
}
function stateCollapsed() {
	collapsePrefix('s_');
	collapsePrefix('o_');
	collapsePrefix('p_');
	showSections();
	markState(1);
}
function stateSections() {
	expandPrefix('s_');
	collapsePrefix('o_');
	collapsePrefix('p_');
	showSections();
	markState(2);
}
function stateGlobals() {
	expandPrefix('s_');
	expandPrefix('o_');
	collapsePrefix('p_');
	hideSections();
	markState(3);
}
function stateSectionsGlobals() {
	expandPrefix('s_');
	expandPrefix('o_');
	collapsePrefix('p_');
	showSections();
	markState(4);
}
function stateGlobalsExpanded() {
	expandPrefix('s_');
	expandPrefix('o_');
	expandPrefix('p_');
	hideSections();
	markState(5);
}
function stateSectionsGlobalsExpanded() {
	expandPrefix('s_');
	expandPrefix('o_');
	expandPrefix('p_');
	showSections();
	markState(6);
}

function switchTheme() {
	if (document.body.classList.contains('dark')) {
		document.body.classList.remove('dark');
	} else {
		document.body.classList.add('dark');
	}
}

function recursiveExpand(id) {
	var el = document.getElementById(id);
	while (el != null) {
		if (el.localName == 'DIV' && el.id.match('h_') == null) {
			expand(el);
		}
		el = el.parentNode;
	}
}

function getByteInterval(elem) {
	let byteAttr = elem.getAttribute('byte');
	if (byteAttr) {
		let p = byteAttr.split(';');
		let base = parseInt(p[0]);
		if (p.length == 1) return [base, base + 1];
		return [base, base + parseInt(p[1])];
	}
}
function isByteSpacing(elem) {
	let byteSpacingAttr = elem.getAttribute('byte-spacing');
	return byteSpacingAttr != null;
}
function findByteInterval(elem) {
	while (elem instanceof HTMLElement) {
		let interval = getByteInterval(elem);
		if (interval) return interval;
		if (isByteSpacing(elem)) return false;
		elem = elem.parentNode;
	}
}
function elementsOnInterval(interval) {
	let elems = [];
	let [start, end] = interval;
	for (var b = start; b < end; b++) {
		elems.push(...byteIndex[b]);
	}
	return elems;
}
function intervalUnion(interval1, interval2) {
	if (!interval1) return interval2;
	if (!interval2) return interval1;
	const [start1, end1] = interval1;
	const [start2, end2] = interval2;
	return [Math.min(start1, start2), Math.max(end1, end2)];
}
function intervalDiff(interval1, interval2) {
	if (!interval1) return interval2;
	if (!interval2) return interval1;
	const [start1, end1] = interval1;
	const [start2, end2] = interval2;
	if (start1 == start2) return [Math.min(end1, end2), Math.max(end1, end2)];
	if (end1 == end2) return [Math.min(start1, start2), Math.max(start1, start2)];
	return [Math.min(start1, start2), Math.max(end1, end2)];
}
function isSubInterval(interval1, interval2) {
	if (!interval2) return false;
	const [start1, end1] = interval1;
	const [start2, end2] = interval2;
	return start1 >= start2 && end1 <= end2;
}

var byteIndex;
function buildByteIndex() {
	byteIndex = Array(sizefill.length);
	const elements = document.getElementsByName('byte');
	for (var i = 0; i < elements.length; i++) {
		const e = elements[i];
		const [start, end] = getByteInterval(e);
		for (var b = start; b < end; b++) {
			byteIndex[b] ??= [];
			byteIndex[b].push(e);
		}
	}
}

const CURSOR = 'var(--cursor-color)';
const SELECT = 'var(--select-color)';

var currentHover;
var spacingHover;
var currentSelect;
var startSelect;
function setHover(interval) {
	const diff = intervalDiff(currentHover, interval);
	currentHover = interval;
	spacingHover = interval == false;
	updateHighlight(diff);
	updateOverlay();
}
function updateSelect() {
	const beforeSelect = currentSelect;
	if (startSelect && currentHover) {
		currentSelect = intervalUnion(startSelect, currentHover);
		if (isSubInterval(currentSelect, startSelect)) {
			currentSelect = undefined;
		}
	}
	const diff = intervalDiff(beforeSelect, currentSelect);
	updateHighlight(diff);
	updateOverlay();
}
function updateOverlay() {
	var text = '';
	const measure = currentSelect ?? currentHover;
	const selected = startSelect || currentSelect;
	if (measure) {
		const [start, end] = measure;
		const length = end - start;
		const size = (sizefill[end] - sizefill[start]) / BIT_PRECISION / 8;
		const per_byte = size / length * 8;
		const address = (CODEBASE + start).toString(16).toUpperCase().padStart(8, '0');
		text = address+' +'+length+': '+size.toFixed(2)+' bytes ('+per_byte.toFixed(2)+' bits per byte).';
		if (currentHover && !startSelect) text += ' Drag to measure interval.';
	}
	let overlay = document.getElementById('overlay');
	overlay.textContent = text;
	overlay.style.color = 'var(--text-color)';
	overlay.style.background = selected ? SELECT : CURSOR;
	if (measure || spacingHover) {
		overlay.style.display = 'block';
	} else {
		overlay.style.display = 'none';
	}
}
function updateHighlight(interval) {
	if (!interval) return;
	for (const elem of elementsOnInterval(interval)) {
		const elemInterval = getByteInterval(elem);
		const hover = isSubInterval(elemInterval, currentHover);
		const select = isSubInterval(elemInterval, currentSelect);
		if (hover) {
			elem.style.background = CURSOR;
		} else if (select) {
			elem.style.background = SELECT;
		} else {
			elem.style.background = 'initial';
		}
	}
}

function moveListener(event) {
	var elem = document.elementFromPoint(event.clientX, event.clientY)
	setHover(findByteInterval(elem));
	updateSelect();
}
function downListener(event) {
	if (event.button != 0) return;
	startSelect = currentHover;
	if (startSelect) event.preventDefault();
	updateSelect();
}
function upListener(event) {
	if (event.button != 0) return;
	startSelect = undefined;
	updateSelect();
}

function initialize() {
	if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
		document.body.classList.add('dark');
	}
	stateGlobals();
	buildByteIndex();
	document.addEventListener('mousemove', moveListener, {passive: true});
	document.addEventListener('mousedown', downListener, {passive: false});
	document.addEventListener('mouseup', upListener, {passive: true});
}
)"""";

static const char* htmlHeader1 = R""""(
</script>
<style type='text/css'>
body{
	font-family:monospace;
}

.data{
	border-collapse:collapse;
}
.data td{
	border:0px;
}
.maintable{
	border-collapse:collapse;
	border:0px;
}
.maintable td{
	border:0px;
	padding:0px;
}
.maintable th{
	border:0px;
	padding:0px;
}
.grptable{
	border-collapse:collapse;
	border:0px;
	width:100%%;
}
.grptable td{
	border:0px;
	padding:0px;
}
.grptable th{
	border:0px;
	padding:0px;
}
.address{width:7em;text-align:left;white-space:nowrap;}

.public_symbol_row_expandable th{
	cursor: pointer;
	cursor: hand;
}
.oldsection_symbol_row_expandable th{
	cursor: pointer;
	cursor: hand;
}
.section_symbol_row_expandable th{
	cursor: pointer;
	cursor: hand;
}
#overlay {
	position: fixed;
	width: 100%;
	height: 25px;
	padding: 10px;
	left: 20px;
	right: 20px;
	bottom: 20px;
	font-size: 20px;
	vertical-align: middle;
	color: black;
	display: none;
	box-shadow: 0 0 15px 2px rgb(0,0,0,0.8);
}
.c1{width:7em;text-align:left;white-space:nowrap;}
.c2{width:45em;text-align:right;white-space:nowrap;}
.c3{width:7em;text-align:right;white-space:nowrap;}
.c4{width:7em;text-align:right;white-space:nowrap;}
.c5{width:7em;text-align:right;white-space:nowrap;}
a:link { color: var(--link-color); }
a:visited { color: var(--link-color); }
a:hover { color: var(--link-color); }
a:active { color: var(--link-color); }

:root {
	--bg-color: #ffffff;
	--text-color: #000000;
	--link-color: #444444;
	--table-header-bg: #eef;
	--public-sym-bg: #99c;
	--old-section-bg: #77a;
	--section-bg: #eda;
	--cursor-color: #bbb;
	--select-color: #ddd;
	--label-weight: bold;
}

body.dark {
	--bg-color: #181820;
	--text-color: #d5dade;
	--link-color: #ffffff;
	--table-header-bg: #234;
	--public-sym-bg: #346;
	--old-section-bg: #458;
	--section-bg: #a7850f;
	--cursor-color: #383840;
	--select-color: #282830;
	--label-weight: normal;
}

body {
	background-color: var(--bg-color);
	color: var(--text-color);
}
.private_symbol_row th{
	background-color: var(--table-header-bg);
	font-weight: var(--label-weight);
}
.public_symbol_row th, .public_symbol_row_expandable th{
	background-color: var(--public-sym-bg);
	font-weight: var(--label-weight);
}
.oldsection_symbol_row th, .oldsection_symbol_row_expandable th{
	background-color: var(--old-section-bg);
	font-weight: var(--label-weight);
}
.section_symbol_row th, .section_symbol_row_expandable th{
	background-color: var(--section-bg);
	font-weight: var(--label-weight);
}
</style>
</head><body onload='initialize()'>
<h1>Crinkler compression report</h1>
<p><b>Report for file %s generated by %s on %s</b></p>
)"""";

static const char* htmlHeader2 = R""""(
<a id='state_1' href='#' onclick='stateCollapsed()'>collapsed</a>&nbsp;
<a id='state_2' href='#' onclick='stateSections()'>sections</a>&nbsp;
<a id='state_3' href='#' onclick='stateGlobals()'>globals</a>&nbsp;
<a id='state_4' href='#' onclick='stateSectionsGlobals()'>sections + globals</a>&nbsp;
<a id='state_5' href='#' onclick='stateGlobalsExpanded()'>globals expanded</a>&nbsp;
<a id='state_6' href='#' onclick='stateSectionsGlobalsExpanded()'>sections + globals expanded</a>&nbsp;
<a href='#' onclick='switchTheme()'>switch theme</a>&nbsp;
<table class='maintable'><tr>
<th nowrap class='c1'>&nbsp;Address</th>
<th nowrap class='c2'>Label name</th>
<th nowrap class='c3'>Size</th>
<th nowrap class='c4'>Comp. size</th>
<th nowrap class='c5'>Ratio</th>
</tr>
)"""";

static const char* htmlFooter = R""""(
</table>
<p><a href='http://crinkler.net'>http://www.crinkler.net</a></p>
<p style='height:100px'></p>
<div id='overlay'></div>
</body></html>
)"""";

const int CODE_BYTE_COLUMNS = 15;
const int DATA_BYTE_COLUMNS = 32;
const int CODE_HEX_WIDTH = 32;
const int DATA_HEX_WIDTH = 75;
const int OPCODE_WIDTH = 16;
const int LABEL_COLOR = 0x808080;

static map<string, string> identmap;
static int num_divs[4];
static int num_sections;


struct SizeColor {
	float size;
	const char *text;
	unsigned color;
	unsigned darkColor;
} sizecols[] = {
	{ 0.1f, "0.1", 0x90f090, 0x004000 },
	{ 0.5f, "0.5", 0x30e030, 0x006000 },
	{ 1.0f, "1", 0x00b000, 0x009010 },
	{ 2.0f, "2", 0x008050, 0x00c020 },
	{ 3.0f, "3", 0x006090, 0x00a0a0 },
	{ 5.0f, "5", 0x0020f0, 0x4060d0 },
	{ 7.0f, "7", 0x0000a0, 0x90a0e0 },
	{ 9.0f, "9", 0x000000, 0xe0e0e0 },
	{ 12.0f, "12", 0x900000, 0xe06060 },
	{ 1000000.0f, "a lot of", 0xff0000, 0xd01010 },
};

// Converts a string to an unique identifier consisting of only ['A'-Z']

static string ToIdent(string str) {
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

static int SizeToColorIndex(int size) {
	float bsize = size / (float)BIT_PRECISION;
	for (int i = 0 ; i < sizeof(sizecols)/sizeof(SizeColor) ; i++) {
		if (bsize < sizecols[i].size) {
			return i;
		}
	}
	return sizeof(sizecols)/sizeof(SizeColor) - 1;
}

static int InstructionSize(_DecodedInst *inst, const int *sizefill) {
	int idx = (int)inst->offset - CRINKLER_CODEBASE;
	return sizefill[idx + inst->size] - sizefill[idx];
}

static int ToAscii(int c) {
	return (c > 32 && c <= 126 || c >= 160) ? c : '.';
}

static void PrintRow(FILE *out, Hunk& hunk, const int *sizefill, int index, int n, int hex_columns, int hex_width, bool ascii, bool spacing) {
	for(int x = 0; x < hex_columns; x++) {
		if (spacing && x > 0 && (x & 3) == 0) {
			// Spacing between blocks of 4
			fprintf(out, "<td byte-spacing>&nbsp;</td>");
		}
		if (spacing && x == 16) {
			// Extra space after first 16
			fprintf(out, "<td byte-spacing>&nbsp;</td>");
		}

		if (x < n) {
			unsigned char c = 0;
			int size = 0;
			int idx = index + x;
			if (idx < hunk.GetRawSize()) {
				size = sizefill[idx + 1] - sizefill[idx];
				c = hunk.GetPtr()[idx];
			}

			if (ascii) {
				fprintf(out, "<td title='%.2f bits' class='sc%d' name='byte' byte='%d'>"
					"&#x%.2X;</td>", size / (float)BIT_PRECISION, SizeToColorIndex(size), idx, ToAscii(c));
			}
			else {
				fprintf(out, "<td title='%.2f bits' class='sc%d' name='byte' byte='%d'>"
					"%.2X</td>", size / (float)BIT_PRECISION, SizeToColorIndex(size), idx, c);
			}
		} else {
			if (ascii) {
				fprintf(out, "<td>&nbsp;</td>");
			} else {
				fprintf(out, "<td>&nbsp;&nbsp;</td>");
			}
		}
	}
	if (!ascii) {
		for (int x = hex_columns * 2 + (spacing ? 8 : 0); x < hex_width; x++) {
			fprintf(out, "<td>&nbsp;</td>");
		}
	}
}

// Return the relocation symbol offset bytes into the instruction. Handles off bounds reads gracefully
static Symbol* getRelocationSymbol(_DecodedInst& inst, int offset, map<int, Symbol*>& relocs) {
	if(offset < 0 || offset > (int)inst.size-4)
		return NULL;
	offset += (int)inst.offset-CRINKLER_CODEBASE;

	map<int, Symbol*>::iterator it = relocs.find(offset);
	return it != relocs.end() ? it->second : NULL;
}

static std::string GenerateLabel(Symbol* symbol, int value, map<int, Symbol*>& symbols) {
	char buff[1024];
	int offset = 0;
	if(symbol->flags & SYMBOL_IS_RELOCATEABLE) {
		offset = value - (symbol->value+CRINKLER_CODEBASE);

		// Prefer label over label+offset, eventhough it makes us change symbol
		if(offset != 0) {
			map<int, Symbol*>::iterator it = symbols.find(value-CRINKLER_CODEBASE);
			if(it != symbols.end()) {
				symbol = it->second;
				offset = 0;
			}
		}
	}
	string name = StripCrinklerSymbolPrefix(symbol->name.c_str());
	string ident = ToIdent(symbol->name);
	name = "<a href='#" + ident + "' onclick='recursiveExpand(\"" + ident + "\")'>" + name + "</a>";	// Add link
	if(offset > 0)
		sprintf_s(buff, sizeof(buff), "%s+0x%X", name.c_str(), offset);
	else if(offset < 0)
		sprintf_s(buff, sizeof(buff), "%s-0x%X", name.c_str(), -offset);
	else
		sprintf_s(buff, sizeof(buff), "%s", name.c_str());
	return buff;
}

// Finds a hexnumber, removes it from the string and returns its position. Value is set to the value of the number
static void ExtractNumber(string& str, string::size_type begin, int& value, string::size_type& startpos, string::size_type& endpos) {
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

static string CalculateInstructionOperands(_DecodedInst& inst, Hunk& hunk, map<int, Symbol*>& relocs, map<int, Symbol*> symbols) {
	string operands = (char*)inst.operands.p;

	// Find number operands in textual assembly
	int number1_value = 0;
	string::size_type number1_startpos = string::npos, number1_endpos = string::npos;			// Immediate or displace
	ExtractNumber(operands, 0, number1_value, number1_startpos, number1_endpos);
	int number2_value = 0;
	string::size_type number2_startpos = string::npos, number2_endpos = string::npos;			// Displace
	if(number1_startpos != string::npos) {
		ExtractNumber(operands, number1_startpos+1, number2_value, number2_startpos, number2_endpos);
		if(number2_startpos != string::npos && operands[number1_endpos] == ']') {
			swap(number1_startpos, number2_startpos);
			swap(number1_endpos, number2_endpos);
			swap(number1_value, number2_value);
		}
	}

	// Find relocation
	Symbol* reloc_symbol1 = getRelocationSymbol(inst, inst.size-4, relocs);	// imm32 or disp32
	Symbol* reloc_symbol2 = NULL;	// disp32

	reloc_symbol2 = getRelocationSymbol(inst, inst.size-5, relocs);	// disp32 followed by imm8
	if(reloc_symbol2 == NULL)
		reloc_symbol2 = getRelocationSymbol(inst, inst.size-6, relocs);	// disp32 followed by imm16
	if(reloc_symbol2 == NULL)
		reloc_symbol2 = getRelocationSymbol(inst, inst.size-8, relocs);	// disp32 followed by imm32

	if(number1_startpos != string::npos && (reloc_symbol1 || reloc_symbol2)) {		// At least one number operand needs a fix
		if(number2_startpos == string::npos) {
			if(reloc_symbol1 == NULL)
				swap(reloc_symbol1, reloc_symbol2);	// Force symbol1 into number1
		}

		int delta = 0;	// The amount label2 will need to be displaced
		if(reloc_symbol1) {
			string label = GenerateLabel(reloc_symbol1, number1_value, symbols);
			delta = int(label.size() - (number1_endpos-number1_startpos));
			operands.erase(number1_startpos, number1_endpos-number1_startpos);
			operands.insert(number1_startpos, label);
		}
		if(reloc_symbol2) {
			if(number2_startpos < number1_startpos)
				delta = 0;
			string label = GenerateLabel(reloc_symbol2, number2_value, symbols);
			operands.erase(number2_startpos+delta, number2_endpos-number2_startpos);
			operands.insert(number2_startpos+delta, label);
		}
	}

	// Handle 8 and 16-bit displacements
	int value;
	if(sscanf_s(operands.c_str(), "%X", &value)) {
		map<int, Symbol*>::iterator it = symbols.find(value-CRINKLER_CODEBASE);
		if(it != symbols.end()) {
			operands = GenerateLabel(it->second, value, symbols);
		}
	}

	return operands;
}

static void HtmlReportRecursive(CompressionReportRecord* csr, FILE* out, Hunk& hunk, Hunk& untransformedHunk, const int* sizefill, bool iscode, map<int, Symbol*>& relocs, map<int, Symbol*>& symbols, map<string, int>& opcodeCounters,
		const char *exefilename, int filesize, Crinkler *crinkler) {
	string divstr;
	// Handle enter node events
	if(csr->type & RECORD_ROOT) {
		// Header until script
		fprintf(out, htmlHeader0);

		// Script
		fprintf(out, "const CODEBASE = %d;\n", CRINKLER_CODEBASE);
		fprintf(out, "const BIT_PRECISION = %d;\n", BIT_PRECISION);
		fprintf(out, "const sizefill = [");
		for (int i = 0; i <= hunk.GetRawSize(); i++) {
			fprintf(out, "%s%d", i > 0 ? "," : "", sizefill[i]);
		}
		fprintf(out, "];\n");
		fprintf(out, script);

		// Header after script
		time_t rawtime;
		struct tm * timeinfo;
		time ( &rawtime );
		timeinfo = localtime ( &rawtime );
		fprintf(out, htmlHeader1, exefilename, CRINKLER_WITH_VERSION, asctime(timeinfo));

		// Print option string
		fprintf(out, "<p><b>Options:");
		crinkler->PrintOptions(out);
		fprintf(out, "</b></p>");

		// Print file size
		fprintf(out, "<p><b>Output file size: %d</b></p>", filesize);

		fprintf(out, "<p><table><tr><td rowspan='2'><b>Bits per byte:</b></td><td>&nbsp;&nbsp;</td>");
		int ncols = sizeof(sizecols) / sizeof(SizeColor);
		for (int i = 0; i < ncols; i++) {
			fprintf(out, "<td class='sc%d' style='background-color:currentColor' colspan='2' title='", i);
			if (i == 0) {
				fprintf(out, "Less than %s bits per byte", sizecols[i].text);
			} else if (i == ncols - 1) {
				fprintf(out, "More than %s bits per byte", sizecols[i - 1].text);
			} else {
				fprintf(out, "Between %s and %s bits per byte", sizecols[i - 1].text, sizecols[i].text);
			}
			fprintf(out, "'>&nbsp;&nbsp;&nbsp;&nbsp;</td>");
		}
		fprintf(out, "</tr>\n<tr><td colspan='2' align='center'></td>");
		for (int i = 0; i < ncols - 1; i++) {
			fprintf(out, "<td colspan='2' align='center'>%s</td>", sizecols[i].text);
		}
		fprintf(out, "<td></td></tr></table></p>\n");

		fprintf(out, "<style type='text/css'>\n");
		for (int i = 0; i < ncols; i++) {
			fprintf(out, ".sc%d { color: #%.6X; }\n"
				"body.dark .sc%d { color: #%.6X; }\n",
				i, sizecols[i].color, i, sizecols[i].darkColor);
		}
		fprintf(out, "</style>\n");

		fprintf(out, htmlHeader2);
	} else {
		if(csr->type & RECORD_CODE) {
			iscode = true;
		}

		string label;
		string css_class;
		string script;
		char div_prefix = 0;
		int level = csr->GetLevel();
		switch(level) {
			case 0:	// Section
				label = csr->name;
				css_class = "section_symbol_row";
				div_prefix = 's';
				break;
			case 1:	// Old section
				label = StripPath(csr->miscString) + ":" + StripCrinklerSymbolPrefix(csr->name.c_str());
				css_class = "oldsection_symbol_row";
				div_prefix  = 'o';
				break;
			case 2:	// Public symbol
				label = StripCrinklerSymbolPrefix(csr->name.c_str());
				css_class = "public_symbol_row";
				div_prefix = 'p';
				break;
			case 3:	// Private symbol
				label = StripCrinklerSymbolPrefix(csr->name.c_str());
				css_class = "private_symbol_row";
				break;
		}

		if(level == 1) {	// Old section
			fprintf(out, "<tr><td nowrap colspan='5'><div id='h_%d'><table class='grptable'>", num_sections++);
		}
		if(csr->children.empty() && (csr->size == 0 || csr->compressedPos < 0))
			div_prefix = 0;
		if(div_prefix) {
			char tmp[512];
			sprintf(tmp, "%c_%d", div_prefix, num_divs[level]++);
			divstr = tmp;
			fprintf(out, "<tr class='%s_expandable' onclick=\"switchMenu('%s');\">", css_class.c_str(), divstr.c_str());
		} else {
			fprintf(out, "<tr class='%s'>", css_class.c_str());
		}

		// Make the label an anchor
		if(!(csr->type & RECORD_DUMMY)) {	// Don't put anchors on dummy records
			label = "<a id='" + ToIdent(csr->name) + "'>" + label + "</a>";
		}
		
		fprintf(out,"<th nowrap class='c1'>%s%.8X&nbsp;</th>"
					"<th nowrap class='c2'>%s</th>", divstr.empty() ? "&nbsp;" : "-", CRINKLER_CODEBASE + csr->pos, label.c_str());

		
		if(csr->size > 0) {
			fprintf(out, "<th nowrap class='c3'>%d</th>", csr->size);
			if(csr->compressedPos >= 0) {	// Initialized data
				fprintf(out,"<th nowrap class='c4'>%.2f</th>"
							"<th nowrap class='c5'>%.1f%%</th>", 
							csr->compressedSize / (BIT_PRECISION *8.0f),
							(csr->compressedSize / (BIT_PRECISION *8.0f)) * 100 / csr->size);
			} else {	// Uninitialized data
				fprintf(out,"<th nowrap class='c4'>&nbsp;</th>"
							"<th nowrap class='c5'>&nbsp;</th>");
			}
		} else {	// Duplicate label
			fprintf(out,"<th nowrap class='c3'>&nbsp;</th>"
						"<th nowrap class='c4'>&nbsp;</th>"
						"<th nowrap class='c5'>&nbsp;</th>");
		}
		fprintf(out,"</tr>");
		if(level == 1) {	// Old section
			fprintf(out, "</table></div></td></tr>");
		}


		if(!divstr.empty())
			fprintf(out,"<tr><td nowrap colspan='5'><div id='%s'><table class='grptable'>", divstr.c_str());
		else
			fprintf(out,"<tr><td nowrap colspan='5'><table class='grptable'>");

		if(csr->children.empty() || csr->pos < csr->children.front()->pos) {
			int size = csr->children.empty() ? csr->size : csr->children.front()->pos - csr->pos;
			
			if(csr->compressedPos >= 0 && size > 0) {
				if (iscode) {
					// Code section

					// Allocate space for worst-case number of instructions
					int instspace = size;
					if (instspace < 15) instspace = 15;
					_DecodedInst *insts = (_DecodedInst *)malloc(instspace * sizeof(_DecodedInst));
					unsigned int numinsts;
					distorm_decode64(CRINKLER_CODEBASE + csr->pos, (unsigned char*)&untransformedHunk.GetPtr()[csr->pos], size, Decode32Bits, insts, instspace, &numinsts);
					for(int i = 0; i < (int)numinsts; i++) {
						int offset = (int)insts[i].offset - CRINKLER_CODEBASE;
						fprintf(out, "<tr name='byte' byte='%d;%d'>", offset, insts[i].size);
						// Address
						fprintf(out, "<td nowrap class='address'>&nbsp;%.8X&nbsp;</td>", (unsigned int)insts[i].offset);
						
						// Hex dump
						fprintf(out, "<td nowrap colspan=4 class='hexdump'><table class='data'><tr>");
						PrintRow(out, hunk, sizefill, offset, insts[i].size, CODE_BYTE_COLUMNS, CODE_HEX_WIDTH, false, false);

						// Disassembly
						// Make hex digits uppercase
						for (unsigned char *cp = insts[i].operands.p ; *cp != 0 ; cp++) {
							if (*cp >= 'a' && *cp <= 'f') *cp += 'A'-'a';
						}
						int size = InstructionSize(&insts[i], sizefill);

						fprintf(out, "<td nowrap title='%.2f bytes' class='sc%d'>%s",
							size / (float)(BIT_PRECISION*8), SizeToColorIndex(size/insts[i].size), insts[i].mnemonic.p);
						for (int j = 0 ; j < OPCODE_WIDTH-(int)insts[i].mnemonic.length ; j++) {
							fprintf(out, "&nbsp;");
						}

						{
							string ops = CalculateInstructionOperands(insts[i], untransformedHunk, relocs, symbols);
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
					{	// Fill row lengths
						int count = 0;
						int idx = csr->pos;
						while(idx < csr->pos+size) {
							if(relocs.find(idx) == relocs.end()) {
								count++;
								if(count == DATA_BYTE_COLUMNS) {
									rowLengths.push_back(DATA_BYTE_COLUMNS);
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
					for(int rowLength : rowLengths) {
						// Print address
						fprintf(out, "<tr><td nowrap class='address'>&nbsp;%.8X&nbsp;</td>", CRINKLER_CODEBASE + idx);

						// Hex dump
						fprintf(out, "<td nowrap colspan=4 class='hexdump'><table class='data'><tr>");
						PrintRow(out, hunk, sizefill, idx, rowLength, DATA_BYTE_COLUMNS, DATA_HEX_WIDTH, false, true);

						map<int, Symbol*>::iterator jt = relocs.find(idx);
						if(jt != relocs.end()) {
							// Write label
							int size = sizefill[idx+4]-sizefill[idx];
							int value = *((int*)&untransformedHunk.GetPtr()[idx]);
							string label = GenerateLabel(jt->second, value, symbols);
							fprintf(out, "<td title='%.2f bytes' class='sc%d' colspan='%d'>%s</td>",
								size / (float)(BIT_PRECISION*8), SizeToColorIndex(size/4), DATA_BYTE_COLUMNS, label.c_str());
						} else {
							// Write ascii
							PrintRow(out, hunk, sizefill, idx, rowLength, DATA_BYTE_COLUMNS, DATA_HEX_WIDTH, true, false);
						}
						fprintf(out, "</tr></table></td></tr>");

						idx += rowLength;
					}
				}
			}
		}
	}

	for(CompressionReportRecord* record : csr->children)
		HtmlReportRecursive(record, out, hunk, untransformedHunk, sizefill, iscode, relocs, symbols, opcodeCounters,
			exefilename, filesize, crinkler);

	// Handle leave node event
	if(csr->type & RECORD_ROOT) {
		fprintf(out,htmlFooter);
	} else {
		fprintf(out,"</table>%s</td></tr>", divstr.empty() ? "" : "</div>");
	}
}

void HtmlReport(CompressionReportRecord* csr, const char* filename, Hunk& hunk, Hunk& untransformedHunk, const int* sizefill,
		const char *exefilename, int filesize, Crinkler *crinkler) {
	identmap.clear();
	num_divs[0] = num_divs[1] = num_divs[2] = num_divs[3] = 0;
	num_sections = 0;
	
	map<int, Symbol*> relocs = hunk.GetOffsetToRelocationMap();
	map<int, Symbol*> symbols = hunk.GetOffsetToSymbolMap();
	FILE* out;
	map<string, int> opcodeCounters;
	if(fopen_s(&out, filename, "wb")) {
		Log::Error(filename, "Cannot open file for writing");
		return;
	}
	HtmlReportRecursive(csr, out, hunk, untransformedHunk, sizefill, false, relocs, symbols, opcodeCounters,
		exefilename, filesize, crinkler);

	fclose(out);
}
