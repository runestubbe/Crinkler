#pragma once
#ifndef _COMPRESSION_STREAM_H_
#define _COMPRESSION_STREAM_H_

#include <vector>

#include "AritCode.h"
#include "ModelList.h"

struct HashBits {
	std::vector<unsigned>	hashes;
	std::vector<bool>		bits;
	std::vector<int>		weights;
	unsigned int			tinyhashsize;
};

struct TinyHashEntry {
	unsigned int	hash;
	unsigned char	prob[2];
	unsigned char	used;
};

class CompressionStream {
	AritState		m_aritstate;
	unsigned char*	m_data;
	int*			m_sizefill;
	int*			m_sizefillptr;
	int				m_maxsize;
	bool			m_saturate;
public:
	CompressionStream(unsigned char* data, int* sizefill, int maxsize, bool saturate);
	
	void	CompressFromHashBits(const HashBits& hashbits, TinyHashEntry* hashtable, int baseprob, int hashsize);
	int		EvaluateSize(const unsigned char* data, int size, const ModelList4k& models, int baseprob, char* context, int bitpos);
	int		Close();
};

HashBits ComputeHashBits(const unsigned char* d, int size, unsigned char* context, const ModelList4k& models, bool first, bool finish);

#endif
