#include "HtmlSummary.h"

#include <cstdio>
#include <algorithm>

#include "Compressor/Compressor.h"
#include "Log.h"
#include "Hunk.h"
#include "Crinkler.h"
#include "distorm.h"


using namespace std;

const int NUM_COLUMNS = 16;
const int OPCODE_WIDTH = 11;

struct scol {
	float size;
	unsigned color;
} sizecols[] = {
	{ 0.1f, 0x80ff80 },
	{ 0.5f, 0x00ff00 },
	{ 1.0f, 0x00a000 },
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
	int size = sizefill[idx + inst->size] - sizefill[idx];
	size /= inst->size;
	return size;
}

static int toAscii(int c) {
	return (c >= 32 && c <= 126 || c >= 160) ? c : '.';
}

static void printRow(FILE *out, const unsigned char *data, const int *sizefill, int index, int n, int datasize, bool ascii) {
	for(int x = 0; x < n; x++) {
		unsigned char c = 0;
		int size = 0;
		int idx = index + x;
		if(idx < datasize) {
			size = sizefill[idx+1]-sizefill[idx];
			c = data[idx++];
		}

		if (ascii) {
			fprintf(out,"<td title=\"%.2f bits\" style=\"color: #%.6X;\">"
				"%c</td>", size / (float)BITPREC, sizeToColor(size), toAscii(c));
		} else {
			fprintf(out,"<td title=\"%.2f bits\" style=\"color: #%.6X;\">"
				"%.2X</td>", size / (float)BITPREC, sizeToColor(size), c);
		}
	}

}

void htmlSummaryRecursive(CompressionSummaryRecord* csr, FILE* out, const unsigned char* data, int datasize, const int* sizefill, bool iscode) {
	//handle enter node events
	if(csr->type & RECORD_ROOT) {
		//write header
		fprintf(out,
			"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">"
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
			"</style>"

			"</head><body>");
	} else {
		if(csr->type & RECORD_SECTION) {
			fprintf(out,
				"<h1>%s</h1>"
				"<table class=\"outertable\">"
				"<tr>"
				"<th>address</th>"
				"<th>label name</th>"
				"<th>size</th>",
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

			fprintf(out, "<tr><td><table class=\"data\"><tr><td class=\"address\">%.8X&nbsp;</td></tr></table></td>",
					CRINKLER_CODEBASE + csr->pos);

			if(csr->type & RECORD_PUBLIC)
				fprintf(out, "<td><b>%s</b></td>", csr->name.c_str());	//emphasize global labels
			else
				fprintf(out, "<td>%s</td>", csr->name.c_str());

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
					fprintf(out,
						"<td align=right>%d</td>",
						csr->size);
				}
			} else {
				fprintf(out, "<td/><td/><td/>");
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
					distorm_decode(CRINKLER_CODEBASE + csr->pos, &data[csr->pos], csr->size, Decode32Bits, insts, instspace, &numinsts);

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
						printRow(out, data, sizefill, (int)insts[i].offset - CRINKLER_CODEBASE, insts[i].size, datasize, false);
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

						fprintf(out, "<tr><td title=\"%.2f bits/byte\" style=\"color: #%.6X;\">%s",
							size / (float)BITPREC, sizeToColor(size), insts[i].mnemonic.p);
						for (int j = 0 ; j < OPCODE_WIDTH-insts[i].mnemonic.length ; j++) {
							fprintf(out, "&nbsp;");
						}
						fprintf(out, "&nbsp;%s</td></tr>", insts[i].operands.p);
					}
					fprintf(out, "</table></td></tr>");

					free(insts);

				} else {
					// Data section

					// Print adresses
					{
						fprintf(out, "<tr><td><table class=\"data\">");
						int bytesleft = csr->size;
						int idx = csr->pos;
						while(bytesleft > 0) {
							int ncolumns = min(bytesleft, NUM_COLUMNS);

							fprintf(out, "<tr>");
							fprintf(out, "<td class=\"address\">%.8X&nbsp;</td>", CRINKLER_CODEBASE + idx);
							fprintf(out, "</tr>");

							bytesleft -= ncolumns;
							idx += ncolumns;	
						}
						fprintf(out, "</table></td>");
					}

					//write hex
					{
						fprintf(out, "<td><table class=\"data\">");
						int bytesleft = csr->size;
						int idx = csr->pos;
						while(bytesleft > 0) {
							int ncolumns = min(bytesleft, NUM_COLUMNS);

							fprintf(out, "<tr>");
							printRow(out, data, sizefill, idx, ncolumns, datasize, false);
							fprintf(out, "</tr>");

							bytesleft -= ncolumns;
							idx += ncolumns;	
						}
						fprintf(out, "</table></td>");
					}
					

					//write ascii
					{
						fprintf(out, "<td colspan=3><table class=\"data\">");
						int bytesleft = csr->size;
						int idx = csr->pos;
						while(bytesleft > 0) {
							int ncolumns = min(bytesleft, NUM_COLUMNS);

							fprintf(out, "<tr>");
							printRow(out, data, sizefill, idx, ncolumns, datasize, true);
							fprintf(out, "</tr>");

							bytesleft -= ncolumns;
							idx += ncolumns;	
						}
						fprintf(out, "</table></td></tr>");
					}
					
				}

			}
		}
	}

	for(vector<CompressionSummaryRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++)
		htmlSummaryRecursive(*it, out, data, datasize, sizefill, iscode);

	//handle leave node event
	if(csr->type == RECORD_ROOT) {
		fprintf(out, "</body></html>");
	} else {
		if(csr->type == RECORD_SECTION) {
			fprintf(out,"</table>");
		}
	}

}

void htmlSummary(CompressionSummaryRecord* csr, const char* filename, const unsigned char* data, int datasize, const int* sizefill) {
	FILE* out;
	if(fopen_s(&out, filename, "wb")) {
		Log::error(0, "", "could not open '%s' for writing", filename);
		return;
	}
	htmlSummaryRecursive(csr, out, data, datasize, sizefill, false);

	fclose(out);
}
