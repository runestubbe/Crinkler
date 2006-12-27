#ifndef _COMPRESSION_STREAM_H_
#define _COMPRESSION_STREAM_H_

extern "C" {
	#include "aritcode.h"
}
#include "ModelList.h"

class CompressionStream {
	AritState	m_aritstate;
	unsigned char*		m_data;
	int*		m_sizefill;
	int*		m_sizefillptr;
	int			m_maxsize;
	char		m_context[8];
public:
	CompressionStream(unsigned char* data, int* sizefill, int maxsize);
	~CompressionStream();

	void Compress(const unsigned char* data, int size, const ModelList& models, int baseprobs[8], int hashsize, bool finish);
	int EvaluateSize(const unsigned char* d, int size, const ModelList& models, int baseprobs[8], char* context);
	int EvaluateSizeQuick(const unsigned char* d, int size, const ModelList& models, int baseprobs[8], char* context, int bitpos);
	int EvaluateSizeQuick_OLD(const unsigned char* d, int size, const ModelList& models, int baseprobs[8], char* context);
	int close();
};

#endif
