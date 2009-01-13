#ifndef _HTML_REPORT_H_
#define _HTML_REPORT_H_

struct CompressionReportRecord;
class Hunk;
class Crinkler;
void htmlReport(CompressionReportRecord* csr, const char* filename, Hunk& hunk, Hunk& untransformedHunk, const int* sizefill, const char *exefilename, Crinkler *crinkler);

#endif