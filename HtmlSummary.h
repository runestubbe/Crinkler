#ifndef _HTML_SUMMARY_H_
#define _HTML_SUMMARY_H_

struct CompressionSummaryRecord;
class Hunk;
void htmlSummary(CompressionSummaryRecord* csr, const char* filename, Hunk& hunk, const int* sizefill);

#endif