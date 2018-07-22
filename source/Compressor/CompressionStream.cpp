#include "CompressionStream.h"
#include "Compressor.h"
#include <memory>
#include <ctime>
#include <cstdio>
#include <xmmintrin.h>
#include <intrin.h>
#include <ppl.h>

#include "model.h"
#include "aritcode.h"
#include "..\misc.h"

using namespace std;

struct Weights;

const int MAX_N_MODELS = 32;

void updateWeights(Weights *w, int bit, bool saturate);

static int nextPowerOf2(int v) {
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

	unsigned int tinyhashsize = nextPowerOf2(bitlength*nmodels);
	TinyHashEntry* hashtable = new TinyHashEntry[tinyhashsize];
	memset(hashtable, 0, tinyhashsize*sizeof(TinyHashEntry));
	TinyHashEntry* hashEntries[MAX_N_MODELS];

	if(first) {	//encode start bit
		int bit = 1;

		// Query models
		unsigned int probs[2] = { (unsigned int)baseprob, (unsigned int)baseprob };
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
			updateWeights((Weights*)hashEntries[m]->prob, bit, m_saturate);
		}
	}
	

	for(int bitpos = 0 ; bitpos < bitlength; bitpos++) {
		int bit = GetBit(data, bitpos);

		if((bitpos&7)==0 && m_sizefillptr) {
			*m_sizefillptr++ = AritCodePos(&m_aritstate)/(BITPREC_TABLE/BITPREC);
		}

		// Query models
		unsigned int probs[2] = { (unsigned int)baseprob, (unsigned int)baseprob };
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
			updateWeights((Weights*)hashEntries[m]->prob, bit, m_saturate);
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

__forceinline uint32_t Hash(const byte* data, __m128i& masked_contextdata, __m128i mask)
{
	__m128i scrambler = _mm_set_epi8(113, 23, 5, 17, 13, 11, 7, 19, 3, 23, 29, 31, 37, 41, 43, 47);
	masked_contextdata = _mm_and_si128(*(__m128i *)data, mask);
	__m128i sample = _mm_mullo_epi16(masked_contextdata, scrambler);
	sample = _mm_add_epi32(_mm_add_epi32(sample, _mm_shuffle_epi32(sample, _MM_SHUFFLE(1, 1, 1, 1))), _mm_shuffle_epi32(sample, _MM_SHUFFLE(2, 2, 2, 2)));
	uint32_t hash = _mm_cvtsi128_si32(sample);

	uint64_t tmp = (uint64_t)hash * 0xd451151b;
	return (uint32_t)tmp ^ uint32_t(tmp >> 32);
}

struct state
{
	uint16_t next_state[2];
	uint16_t boosted_counters[2];
};

int CompressionStream::EvaluateSize(const unsigned char* d, int size, const ModelList& models, int baseprob, char* context, int bitpos) {
	unsigned char* data = new unsigned char[size + MAX_CONTEXT_LENGTH + 16];	// ensure 128bit operations are safe
	memcpy(data, context, MAX_CONTEXT_LENGTH);
	data += MAX_CONTEXT_LENGTH;
	memcpy(data, d, size);

	unsigned int tinyhashsize = nextPowerOf2(size*3/2);
	unsigned int tinyhashmask = tinyhashsize - 1u;
	int* hash_positions = new int[tinyhashsize];
	byte* hash_probs = new byte[tinyhashsize*2];

	unsigned int* sums = new unsigned int[size*2];	//summed predictions

	for(int i = 0; i < size; i++) {
		sums[i*2] = baseprob;
		sums[i*2+1] = baseprob;
	}

	int bytemask = (0xff00 >> bitpos);
	int nmodels = models.nmodels; 
	for(int modeli = 0; modeli < nmodels; modeli++)
	{
		//clear hashtable
		for(unsigned int i = 0; i < tinyhashsize; i++)
		{
			hash_positions[i] = -1;
		}

		int weight = models[modeli].weight;
		unsigned char w = (unsigned char)models[modeli].mask; 

		unsigned char maskbytes[16] = {};
		for(int i = 0; i < 8; i++) {
			maskbytes[i] = ((w >> i) & 1) * 0xff;
		}
		maskbytes[8] = bytemask;
		__m128i mask = *(__m128i*)maskbytes;

		__m128i next_masked_contextdata;
		unsigned int next_tinyhash;

		next_tinyhash = Hash(data - MAX_CONTEXT_LENGTH, next_masked_contextdata, mask) & tinyhashmask;
		
		for(int pos = 0; pos < size; pos++) {
			int bit = (data[pos] >> (7-bitpos)) & 1;

			__m128i masked_contextdata = next_masked_contextdata;
			size_t tinyhash = next_tinyhash;
			next_tinyhash = Hash(data + pos + 1 - MAX_CONTEXT_LENGTH, next_masked_contextdata, mask) & tinyhashmask;

			while(true)
			{
				int candidate_pos = hash_positions[tinyhash];
				if(candidate_pos == -1)
				{
					hash_positions[tinyhash] = pos;

					//update weights
					size_t bit_idx = tinyhash * 2 + bit;
					hash_probs[bit_idx] = 1;
					hash_probs[bit_idx ^ 1] = 0;
					break;
				}
				
				if(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(*(__m128i *)&data[candidate_pos - MAX_CONTEXT_LENGTH], mask), masked_contextdata)) == 0xFFFF)
				{
					int p0 = hash_probs[tinyhash * 2 + 0];
					int p1 = hash_probs[tinyhash * 2 + 1];

					unsigned int shift = weight - (((p0 - 1)|(p1 - 1)) >> 31) * 2;
					sums[pos * 2 + 0] += (p0 << shift);
					sums[pos * 2 + 1] += (p1 << shift);

					//update weights
					size_t bit_idx = tinyhash * 2 + bit;
					size_t not_bit_idx = bit_idx ^ 1;
					if(!m_saturate || hash_probs[bit_idx] < 255) hash_probs[bit_idx] += 1;
					if(hash_probs[not_bit_idx] > 1) hash_probs[not_bit_idx] >>= 1;
					break;
				}
					
				tinyhash = (tinyhash + 1) & tinyhashmask;
			}
		}
	}

	uint64_t totalsize = 0;
	for(int pos = 0; pos < size; pos++) {
		int bit = (data[pos] >> (7 - bitpos)) & 1;
		totalsize += AritSize2(sums[pos * 2 + bit], sums[pos * 2 + !bit]);
	}
	
	delete[] hash_positions;
	delete[] hash_probs;

	data -= MAX_CONTEXT_LENGTH;
	delete[] data;
	delete[] sums;

	return (int) (totalsize / (BITPREC_TABLE / BITPREC));
}

CompressionStream::CompressionStream(unsigned char* data, int* sizefill, int maxsize, bool saturate) :
m_data(data), m_sizefill(sizefill), m_sizefillptr(sizefill), m_maxsize(maxsize), m_saturate(saturate)
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
