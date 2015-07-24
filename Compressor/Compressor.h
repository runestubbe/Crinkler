#pragma once
#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

const int MAX_INPUT_SIZE = 128000;
const int MAX_CONTEXT_LENGTH = 8;

enum CompressionType {COMPRESSION_INSTANT, COMPRESSION_FAST, COMPRESSION_SLOW, COMPRESSION_VERYSLOW};


#include "aritcode.h"
#include "CompressionStream.h"
#include "ProgressBar.h"
#include "ModelList.h"

class CompressedData {
	char* m_data;
	int m_size;
public:
	CompressedData(char* data, int size) :
		m_data(data), m_size(size)
	{
		;
	}
	~CompressedData() {
		delete[] m_data;
	}
	const char* getPtr() const { return m_data; };
	int getSize() const { return m_size; };
};

//Approximates the models for a given data chunk
ModelList1k ApproximateModels1k(const unsigned char* data, int datasize, int* compsize, ProgressBar* progressBar, bool verbose);
ModelList ApproximateModels4k(const unsigned char* data, int datasize, int baseprob, bool saturate, int* compsize, ProgressBar* progressBar, bool verbose, CompressionType compressionType, char* context);
ModelList InstantModels();

int Compress1K(unsigned char* data, int size, unsigned char* compressed, int compressed_size, int boost_factor, int b0, int b1, unsigned int modelmask, int* sizefill, int* internal_size);

const char *compTypeName(CompressionType ct);

#endif
