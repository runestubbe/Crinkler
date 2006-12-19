#ifndef _COMPRESSION_SUMMARY_H_
#define _COMPRESSION_SUMMARY_H_

#include "Hunk.h"
class CompressionSummary {
public:
	CompressionSummary();
	~CompressionSummary();

	void Show(CompressionSummaryRecord* csr);
};

#endif