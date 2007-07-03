#ifndef _HTML_SUMMARY_H_
#define _HTML_SUMMARY_H_

struct CompressionSummaryRecord;
void htmlSummary(CompressionSummaryRecord* csr, const char* filename, const unsigned char* data, int datasize, const int* sizefill);

#endif