#include "CompressionStream.h"
#include <memory>
#include <ctime>
#include <cstdio>
#include <mmintrin.h>
#include <intrin.h>
#include <ppl.h>

#include "model.h"
#include "aritcode.h"
#include "..\misc.h"

using namespace std;

struct Weights;

const int MAX_CONTEXT_LENGTH = 8;
const int MAX_N_MODELS = 32;

void updateWeights(Weights *w, int bit);

static int previousPowerOf2(int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	return v+1;
}

struct TinyHashEntry {
	unsigned int hash;
	unsigned char prob[2];
	unsigned char used;
};

void CompressionStream::Compress(const unsigned char* d, int size, const ModelList& models, int baseprob, int hashsize, bool first, bool finish) {
	hashsize /= 2;
	int bitlength = size*8;
	unsigned char* data = new unsigned char[size+MAX_CONTEXT_LENGTH];
	memcpy(data, m_context, MAX_CONTEXT_LENGTH);
	data += MAX_CONTEXT_LENGTH;
	memcpy(data, d, size);
		
	unsigned int weightmasks[MAX_N_MODELS];
	unsigned char masks[MAX_N_MODELS];
	int nmodels = models.nmodels;
	unsigned int w = models.getMaskList(masks, finish);
	int weights[MAX_N_MODELS];

	int v = 0;
	for(int n = 0 ; n < models.nmodels ; n++) {
		while (w & 0x80000000) {
			w <<= 1;
			v++;
		}
		w <<= 1;
		weights[n] = v;
		weightmasks[n] = (unsigned int)masks[n] | (w & 0xFFFFFF00);
	}

	unsigned int tinyhashsize = previousPowerOf2(bitlength*nmodels);
	TinyHashEntry* hashtable = new TinyHashEntry[tinyhashsize];
	memset(hashtable, 0, tinyhashsize*sizeof(TinyHashEntry));
	TinyHashEntry* hashEntries[MAX_N_MODELS];

	if(first) {	//encode start bit
		int bit = 1;

		// Query models
		unsigned int probs[2] = { baseprob, baseprob };
		for(int m = 0 ; m < nmodels; m++) {
			unsigned int hash = ModelHashStart(weightmasks[m], HASH_MULTIPLIER) % hashsize;
			unsigned int tinyHash = hash & (tinyhashsize-1);
			TinyHashEntry *he = &hashtable[tinyHash];
			while(he->hash != hash && he->used == 1) {
				tinyHash++;
				if(tinyHash >= tinyhashsize)
					tinyHash = 0;
				he = &hashtable[tinyHash];
			}

			he->hash = hash;
			he->used = 1;
			hashEntries[m] = he;

			int fac = weights[m];
			unsigned int shift = (1 - (((he->prob[0]+255)&(he->prob[1]+255)) >> 8))*2 + fac;
			probs[0] += ((unsigned int)he->prob[0] << shift);
			probs[1] += ((unsigned int)he->prob[1] << shift);
		}

		// Encode bit
		AritCode(&m_aritstate, probs[1], probs[0], 1-bit);

		// Update models
		for(int m = 0; m < models.nmodels; m++) {
			updateWeights((Weights*)hashEntries[m]->prob, bit);
		}
	}
	

	for(int bitpos = 0 ; bitpos < bitlength; bitpos++) {
		int bit = GetBit(data, bitpos);

		if((bitpos&7)==0 && m_sizefillptr) {
			*m_sizefillptr++ = AritCodePos(&m_aritstate)/(BITPREC_TABLE/BITPREC);
		}

		// Query models
		unsigned int probs[2] = { baseprob, baseprob };
		for(int m = 0 ; m < nmodels; m++) {
			unsigned int hash = ModelHash(data, bitpos, weightmasks[m], HASH_MULTIPLIER) % hashsize;
			unsigned int tinyHash = hash & (tinyhashsize-1);
			TinyHashEntry *he = &hashtable[tinyHash];
			while(he->hash != hash && he->used == 1) {
				tinyHash++;
				if(tinyHash >= tinyhashsize)
					tinyHash = 0;
				he = &hashtable[tinyHash];
			}
			
			he->hash = hash;
			he->used = 1;
			hashEntries[m] = he;

			int fac = weights[m];
			unsigned int shift = (1 - (((he->prob[0]+255)&(he->prob[1]+255)) >> 8))*2 + fac;
			probs[0] += ((unsigned int)he->prob[0] << shift);
			probs[1] += ((unsigned int)he->prob[1] << shift);
		}

		// Encode bit
		AritCode(&m_aritstate, probs[1], probs[0], 1-bit);

		// Update models
		for(int m = 0; m < models.nmodels; m++) {
			updateWeights((Weights*)hashEntries[m]->prob, bit);
		}
	}

	if(m_sizefillptr) {
		*m_sizefillptr = AritCodePos(&m_aritstate)/(BITPREC_TABLE/BITPREC);
	}

	delete[] hashtable;
	{	//save context for next call
		int s = min(size, MAX_CONTEXT_LENGTH);
		if(s > 0)
			memcpy(m_context+8-s, data+size-s, s);
	}

	data -= MAX_CONTEXT_LENGTH;
	delete[] data;
}

int CompressionStream::EvaluateSize(const unsigned char* d, int size, const ModelList& models, int baseprob, char* context) {
	int bitlength = size*8;
	unsigned char* data = new unsigned char[size+MAX_CONTEXT_LENGTH];
	memcpy(data, context, MAX_CONTEXT_LENGTH);
	data += MAX_CONTEXT_LENGTH;
	memcpy(data, d, size);

	unsigned int weightmasks[256];
	unsigned char masks[256];
	int nmodels = models.nmodels;
	unsigned int w = models.getMaskList(masks, false);
	int weights[256];

	int n = 0;
	int v = 0;
	while(w != 0 && n < models.nmodels) {
		while (w & 0x80000000) {
			w <<= 1;
			v++;
		}
		w <<= 1;
		weights[n] = v;
		weightmasks[n] = (int)masks[n] | (w & 0xFFFFFF00);
		n++;
	}

	unsigned int* sums = new unsigned int[bitlength*2];	//summed predictions
	for(int i = 0; i < bitlength; i++) {
		sums[i*2] = baseprob;
		sums[i*2+1] = baseprob;
	}

	unsigned int tinyhashsize = previousPowerOf2(bitlength*2);
	TinyHashEntry* hashtable = new TinyHashEntry[tinyhashsize];

	for(int modeli = 0; modeli < nmodels; modeli++) {
		//clear hashtable
		memset(hashtable, 0, tinyhashsize*sizeof(TinyHashEntry));

		for(int bitpos = 0; bitpos < bitlength; bitpos++) {
			int bit = GetBit(data, bitpos);

			unsigned int hash = ModelHash(data, bitpos, weightmasks[modeli], HASH_MULTIPLIER);
			unsigned int tinyHash = hash & (tinyhashsize-1);
			TinyHashEntry *he = &hashtable[tinyHash];
			while(he->hash != hash && he->used == 1) {
				tinyHash++;
				if(tinyHash >= tinyhashsize)
					tinyHash = 0;
				he = &hashtable[tinyHash];
			}

			he->hash = hash;
			he->used = 1;

			int fac = weights[modeli];
			unsigned int shift = (1 - (((he->prob[0]+255)&(he->prob[1]+255)) >> 8))*2 + fac;
			sums[bitpos*2+0] += ((int)he->prob[0] << shift);
			sums[bitpos*2+1] += ((int)he->prob[1] << shift);
			updateWeights((Weights*)he->prob, bit);
		}
	}

	//sum
	long long totalsize = 0;
	//TODO: lift to separate function
#if USE_OPENMP
	#pragma omp parallel for reduction(+:totalsize)
	for(int bitpos = 0; bitpos < bitlength; bitpos++) {
		int bit = GetBit(data, bitpos);
		totalsize += AritSize2(sums[bitpos*2+bit], sums[bitpos*2+!bit]);
	}
#else
	concurrency::combinable<long long> combinable_totalsize;
	const int BLOCK_SIZE = 64;
	int num_blocks = (bitlength + BLOCK_SIZE - 1) / BLOCK_SIZE;
	concurrency::parallel_for(0, num_blocks, [&](int block)
	{
		int start_idx = block * BLOCK_SIZE;
		int end_idx = std::min(start_idx + block, bitlength);
		long long block_size = 0;
		for(int bitpos = start_idx; bitpos < end_idx; bitpos++) {
			int bit = GetBit(data, bitpos);
			block_size += AritSize2(sums[bitpos*2+bit], sums[bitpos*2+!bit]);
		}
		combinable_totalsize.local() += block_size;
	});
	totalsize = combinable_totalsize.combine(std::plus<long long>());
#endif

	delete[] hashtable;

	data -= MAX_CONTEXT_LENGTH;
	delete[] data;
	delete[] sums;

	return (int) (totalsize / (BITPREC_TABLE / BITPREC));
}

inline unsigned int QuickHash(const byte *data, int pos, __m64 mask, int bytemask) {
	__m64 contextdata = *(__m64 *)&data[pos-8];
	__m64 scrambler = _mm_set_pi8(23,5,17,13,11,7,19,3);
	__m64 sample = _mm_mullo_pi16(_mm_and_si64(contextdata, mask), scrambler);
	unsigned int contexthash1 = _mm_cvtsi64_si32(sample);
	unsigned int contexthash2 = _mm_cvtsi64_si32(_mm_srli_si64(sample, 32));
	unsigned int contexthash = contexthash1 ^ contexthash2;
	unsigned char databyte = (unsigned char)(data[pos] & bytemask);
	return contexthash + ((unsigned int)databyte);
}

int CompressionStream::EvaluateSizeQuick(const unsigned char* d, int size, const ModelList& models, int baseprob, char* context, int bitpos) {
	unsigned char* data = new unsigned char[size+MAX_CONTEXT_LENGTH];
	memcpy(data, context, MAX_CONTEXT_LENGTH);
	data += MAX_CONTEXT_LENGTH;
	memcpy(data, d, size);

	unsigned int tinyhashsize = previousPowerOf2(size*2);
	unsigned int recths = ((1<<31)/tinyhashsize-1)*2+1;
	unsigned int recthslog = 0;
	while((1u<<recthslog) <= recths) recthslog++;
	TinyHashEntry* hashtable = new TinyHashEntry[tinyhashsize];

	unsigned int* sums = new unsigned int[size*2];	//summed predictions

	for(int i = 0; i < size; i++) {
		sums[i*2] = baseprob;
		sums[i*2+1] = baseprob;
	}

	int bytemask = (0xff00 >> bitpos);
	int nmodels = models.nmodels; 
	for(int modeli = 0; modeli < nmodels; modeli++) {
		//clear hashtable
		memset(hashtable, 0, tinyhashsize*sizeof(TinyHashEntry));

		int weight = models[modeli].weight;
		unsigned char w = (unsigned char)models[modeli].mask;
		unsigned char maskbytes[8];
		for (int i = 0 ; i < 8 ; i++) {
			maskbytes[i] = ((w >> i) & 1) * 0xff;
		}
		__m64 mask = *(__m64 *)maskbytes;

		for(int pos = 0; pos < size; pos++) {
			int bit = (data[pos] >> (7-bitpos)) & 1;

			unsigned int hash = QuickHash(data, pos, mask, bytemask);
			unsigned int tinyHash = (hash*recths)>>recthslog;
			//unsigned int tinyHash = hash & (tinyhashsize-1);
			TinyHashEntry *he = &hashtable[tinyHash];
			while(he->hash != hash && he->used == 1) {
				tinyHash++;
				if(tinyHash >= tinyhashsize)
					tinyHash = 0;
				he = &hashtable[tinyHash];
			}

			he->hash = hash;
			he->used = 1;

			int fac = weight;
			unsigned int shift = (1 - (((he->prob[0]+255)&(he->prob[1]+255)) >> 8))*2 + fac;
			sums[pos*2+0] += ((int)he->prob[0] << shift);
			sums[pos*2+1] += ((int)he->prob[1] << shift);

			//update weights
			he->prob[bit] += 1;
			if (he->prob[!bit] > 1) he->prob[!bit] >>= 1;
		}
	}

	//sum 
	long long totalsize = 0;
#if USE_OPENMP
	#pragma omp parallel for reduction(+:totalsize)
	for(int pos = 0; pos < size; pos++) {
		int bit = (data[pos] >> (7-bitpos)) & 1;
		totalsize += AritSize2(sums[pos*2+bit], sums[pos*2+!bit]);
	}
#else
	concurrency::combinable<long long> combinable_totalsize;
	const int BLOCK_SIZE = 64;
	int num_blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	concurrency::parallel_for(0, num_blocks, [&](int block)
	{
		int start_idx = block * BLOCK_SIZE;
		int end_idx = std::min(start_idx + BLOCK_SIZE, size);
		long long block_size = 0;
		for(int pos = start_idx; pos < end_idx; pos++) {
			int bit = (data[pos] >> (7-bitpos)) & 1;
			block_size += AritSize2(sums[pos*2+bit], sums[pos*2+!bit]);
		}
		combinable_totalsize.local() += block_size;
	});
	totalsize = combinable_totalsize.combine(std::plus<long long>());
#endif
	
	_mm_empty();

	delete[] hashtable;

	data -= MAX_CONTEXT_LENGTH;
	delete[] data;
	delete[] sums;

	return (int) (totalsize / (BITPREC_TABLE / BITPREC));
}

CompressionStream::CompressionStream(unsigned char* data, int* sizefill, int maxsize) :
	m_data(data), m_sizefill(sizefill), m_sizefillptr(sizefill), m_maxsize(maxsize)
{
	if(data != NULL) {
		memset(m_data, 0, m_maxsize);
		AritCodeInit(&m_aritstate, m_data);
	}
	
	memset(m_context, 0, MAX_CONTEXT_LENGTH);
}

CompressionStream::~CompressionStream() {
}

int CompressionStream::Close(void) {
	return (AritCodeEnd(&m_aritstate) + 7) / 8;
}
