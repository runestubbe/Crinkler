#include "HtmlSummary.h"

#include <cstdio>
#include <algorithm>

#include "Compressor/Compressor.h"
#include "Log.h"
#include "Hunk.h"


using namespace std;

static int sizeToColor(int size) {
	int v = min(size/16, 255);	//between 0 and 2 bytes
	return (v<<16) | ((255-v)<<8);
}

static int toAscii(int c) {
	return (c >= 32 && c <= 126 || c >= 160) ? c : '.';
}

void htmlSummaryRecursive(CompressionSummaryRecord* csr, FILE* out, const unsigned char* data, int datasize, const int* sizefill) {
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
				"<th>label name</th>"
				"<th>pos</th>"
				"<th>comp-pos</th>"
				"<th>size</th>"
				"<th>compsize</th>"
				"</tr>", csr->name.c_str());
		} else {
			if(csr->type & RECORD_PUBLIC)
				fprintf(out, "<tr><td><b>%s</b></td>", csr->name.c_str());	//emphasize global labels
			else
				fprintf(out, "<tr><td>%s</td>", csr->name.c_str());

			if(csr->compressedPos >= 0) {
				fprintf(out,"<td align=right>%9d</td>"
					"<td align=right>%.2f</td>"
					"<td align=right>%d</td>"
					"<td align=right>%.2f</td>",
					csr->pos, csr->compressedPos / (BITPREC*8.0f), csr->size, csr->compressedSize / (BITPREC*8.0f));
			} else {
				fprintf(out,"<td align=right>%9d</td>"
					"<td/>"
					"<td align=right>%d</td>"
					"<td/>",
					csr->pos, csr->size);
			}
			fprintf(out,"</tr>");

			if(csr->compressedPos >= 0 && csr->size > 0) {
				const int NUM_COLUMNS = 32;
				
				//write hex
				{
					int bytesleft = csr->size;
					fprintf(out, "<tr><td><table class=\"data\">");
					int idx = csr->pos;
					while(bytesleft > 0) {
						int ncolumns = min(bytesleft, NUM_COLUMNS);
						fprintf(out, "<tr>");
						fprintf(out, "<td class=\"address\">%.4x</td>", idx);

						for(int x = 0; x < ncolumns; x++) {
							unsigned char c = 0;
							int size = 0;
							if(idx < datasize) {
								size = sizefill[idx+1]-sizefill[idx];
								c = data[idx++];
							}

							fprintf(out,"<td title=\"%.2f bits\" style=\"color: #%.6X;\">"
								"%.2X</td>", size / (float)BITPREC, sizeToColor(size), c);
						}
						fprintf(out, "</tr>");
						bytesleft -= ncolumns;
					}
					fprintf(out, "</table></td>");
				}
				

				//write ascii
				{
					int bytesleft = csr->size;
					fprintf(out, "<td colspan=4><table class=\"data\">");
					int idx = csr->pos;
					while(bytesleft > 0) {
						int ncolumns = min(bytesleft, NUM_COLUMNS);
						fprintf(out, "<tr>");

						for(int x = 0; x < ncolumns; x++) {
							unsigned char c = 0;
							int size = 0;
							if(idx < datasize) {
								size = sizefill[idx+1]-sizefill[idx];
								c = data[idx++];
							}

							fprintf(out,"<td title=\"%.2f bits\" style=\"color: #%.6X;\">"
								"%c</td>", size / (float)BITPREC, sizeToColor(size), toAscii(c));
						}
						fprintf(out, "</tr>");
						bytesleft -= ncolumns;
					}
					fprintf(out, "</table></td></tr>");
				}
			}
		}
	}

	for(vector<CompressionSummaryRecord*>::iterator it = csr->children.begin(); it != csr->children.end(); it++)
		htmlSummaryRecursive(*it, out, data, datasize, sizefill);

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
	htmlSummaryRecursive(csr, out, data, datasize, sizefill);

	fclose(out);
}
