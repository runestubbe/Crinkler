#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

#define MAX_INPUT_SIZE	128000

enum CompressionType {COMPRESSION_INSTANT, COMPRESSION_FAST, COMPRESSION_SLOW, COMPRESSION_VERYSLOW};

extern "C" {
	#include "aritcode.h"
}
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
//method: indicates the approximation method
//flushes: a null terminated list of stream flush positions
ModelList ApproximateModels(const unsigned char* data, int datasize, int baseprobs[8], int* compsize, ProgressBar* progressBar, bool verbose, CompressionType compressionType);


#endif
