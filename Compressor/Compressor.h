#pragma once
#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

const int MAX_INPUT_SIZE = 128000;

enum CompressionType {COMPRESSION_INSTANT, COMPRESSION_FAST, COMPRESSION_SLOW, COMPRESSION_VERYSLOW};


#include "aritcode.h"
#include "CompressionStream.h"
#include "ProgressBar.h"

struct DataPacket {
	char* data;
	int size;
};

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
ModelList ApproximateModels(const unsigned char* data, int datasize, int baseprobs[8], int* compsize, ProgressBar* progressBar, bool verbose, CompressionType compressionType, int modelbits);
ModelList InstantModels();

void TinyCompress(unsigned char* org_data, int size, unsigned char* compressed, int& compressed_size,
				  int& best_boost, int& best_b0, int& best_b1, unsigned int& best_modelmask, int* sizefill);

#endif
