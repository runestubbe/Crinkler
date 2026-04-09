#pragma once
#ifndef _KKP_REPORT_H_
#define _KKP_REPORT_H_

struct CompressionReportRecord;
class Hunk;

void KkpReport(CompressionReportRecord* csr, const char* filename, Hunk& hunk, const int* sizefill);

#endif
