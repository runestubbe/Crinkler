#include "CompressionStream.h"
#include "Compressor.h"
#include <memory>
#include <cstdio>
#include <vector>
#include <xmmintrin.h>
#include <intrin.h>
#include <ppl.h>

#include "Model.h"
#include "AritCode.h"
#include "CounterState.h"

using namespace std;

const int MAX_N_MODELS = 32;

struct Weights;
void UpdateWeights(Weights *w, int bit, bool saturate);

static int NextPowerOf2(int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	return v+1;
}

HashBits ComputeHashBits(const unsigned char* d, int size, unsigned char* context, const ModelList4k& models, bool first, bool finish) {
	int bitlength = first + size * 8;
	int length = bitlength * models.nmodels;
	HashBits out;
	out.hashes.reserve(length);
	out.bits.reserve(bitlength);
	out.weights.resize(models.nmodels);

	out.tinyhashsize = NextPowerOf2(length);

	unsigned char* databuf = new unsigned char[size + MAX_CONTEXT_LENGTH];
	unsigned char* data = databuf + MAX_CONTEXT_LENGTH;
	memcpy(databuf, context, MAX_CONTEXT_LENGTH);
	memcpy(data, d, size);

	unsigned int weightmasks[MAX_N_MODELS];
	unsigned char masks[MAX_N_MODELS];
	int nmodels = models.nmodels;
	unsigned int w = models.GetMaskList(masks, finish);

	int v = 0;
	for (int n = 0; n < models.nmodels; n++) {
		while (w & 0x80000000) {
			w <<= 1;
			v++;
		}
		w <<= 1;
		out.weights[n] = v;
		weightmasks[n] = (unsigned int)masks[n] | (w & 0xFFFFFF00);
	}

	if (first) {	// Encode start bit
		int bit = 1;

		// Query models
		for (int m = 0; m < nmodels; m++) {
			unsigned int hash = ModelHashStart(weightmasks[m], HASH_MULTIPLIER);
			out.hashes.push_back(hash);
		}
		out.bits.push_back(bit);
	}

	for (int bitpos = 0; bitpos < size * 8; bitpos++) {
		int bit = GetBit(data, bitpos);

		// Query models
		for (int m = 0; m < nmodels; m++) {
			unsigned int hash = ModelHash(data, bitpos, weightmasks[m], HASH_MULTIPLIER);
			out.hashes.push_back(hash);
		}
		out.bits.push_back(bit);
	}

	{	// Save context for next call
		int s = min(size, MAX_CONTEXT_LENGTH);
		if (s > 0)
			memcpy(context + MAX_CONTEXT_LENGTH - s, data + size - s, s);
	}

	delete[] databuf;

	return out;
}

void CompressionStream::CompressFromHashBits(const HashBits& hashbits, TinyHashEntry* hashtable, int baseprob, int hashsize) {
	int length = (int)hashbits.hashes.size();
	int nmodels = (int)hashbits.weights.size();
	int bitlength = length / nmodels;
	assert(bitlength * nmodels == length);

	hashsize /= 2;
	uint32_t hashshift = 0;
	while (hashsize > (1ull << hashshift))
		hashshift++;
	uint32_t rcp_hashsize = (((1ull << (hashshift + 31)) + hashsize - 1) / hashsize);
	uint32_t rcp_shift = hashshift - 1u + 32u;

	unsigned int tinyhashsize = hashbits.tinyhashsize;
	memset(hashtable, 0, tinyhashsize * sizeof(TinyHashEntry));
	TinyHashEntry* hashEntries[MAX_N_MODELS];

	int hashpos = 0;
	for (int bitpos = 0; bitpos < bitlength; bitpos++) {
		int bit = hashbits.bits[bitpos];

		if (m_sizefillptr && ((bitpos - bitlength) & 7) == 0) {
			*m_sizefillptr++ = AritCodePos(&m_aritstate) / (TABLE_BIT_PRECISION / BIT_PRECISION);
		}

		// Query models
		unsigned int probs[2] = { (unsigned int)baseprob, (unsigned int)baseprob };
		for (int m = 0; m < nmodels; m++) {
			uint32_t h = hashbits.hashes[hashpos++];
			unsigned int hash = h - uint32_t(((uint64_t)h * rcp_hashsize) >> rcp_shift) * hashsize;

			unsigned int tinyHash = hash & (tinyhashsize - 1);
			TinyHashEntry *he = &hashtable[tinyHash];

			while(true)
			{
				if(he->used == 0) {
					he->hash = hash;
					he->used = 1;
					hashEntries[m] = he;
					break;
				} else if(he->hash == hash) {
					hashEntries[m] = he;

					int fac = hashbits.weights[m];
					unsigned int shift = (1 - (((he->prob[0] + 255)&(he->prob[1] + 255)) >> 8)) * 2 + fac;
					probs[0] += ((unsigned int)he->prob[0] << shift);
					probs[1] += ((unsigned int)he->prob[1] << shift);
					break;
				} else {
					tinyHash++;
					if(tinyHash >= tinyhashsize)
						tinyHash = 0;
					he = &hashtable[tinyHash];
				}
			}
			
			
		}

		// Encode bit
		AritCode(&m_aritstate, probs[1], probs[0], 1 - bit);

		// Update models
		for (int m = 0; m < nmodels; m++) {
			UpdateWeights((Weights*)hashEntries[m]->prob, bit, m_saturate);
		}
	}

	if (m_sizefillptr) {
		*m_sizefillptr = AritCodePos(&m_aritstate) / (TABLE_BIT_PRECISION / BIT_PRECISION);
	}
}

__forceinline uint32_t Hash(__m128i& masked_contextdata)
{
	__m128i scrambler = _mm_set_epi8(113, 23, 5, 17, 13, 11, 7, 19, 3, 23, 29, 31, 37, 41, 43, 47);
	
	__m128i sample = _mm_madd_epi16(masked_contextdata, scrambler);
	sample = _mm_add_epi32(_mm_add_epi32(sample, _mm_shuffle_epi32(sample, _MM_SHUFFLE(1, 1, 1, 1))), _mm_shuffle_epi32(sample, _MM_SHUFFLE(2, 2, 2, 2)));
	uint32_t hash = _mm_cvtsi128_si32(sample);

	uint64_t tmp = (uint64_t)hash * 0xd451151b;
	return (uint32_t)tmp ^ uint32_t(tmp >> 32);
}

int CompressionStream::EvaluateSize(const unsigned char* d, int size, const ModelList4k& models, int baseprob, char* context, int bitpos) {
	unsigned char* data = new unsigned char[size + MAX_CONTEXT_LENGTH + 16];	// Ensure 128bit operations are safe
	memcpy(data, context, MAX_CONTEXT_LENGTH);
	data += MAX_CONTEXT_LENGTH;
	memcpy(data, d, size);

	unsigned int tinyhashsize = NextPowerOf2(size*3/2);
	unsigned int tinyhashmask = tinyhashsize - 1u;
	int* hash_positions = new int[tinyhashsize];
	uint16_t* hash_counter_states = new uint16_t[tinyhashsize];

	unsigned int* sums = new unsigned int[size*2];	// Summed predictions

	for(int i = 0; i < size; i++) {
		sums[i*2] = baseprob;
		sums[i*2+1] = baseprob;
	}
	CounterState* counter_states_ptr = m_saturate ? saturated_counter_states : unsaturated_counter_states;

	// Clear hashtable
	memset(hash_positions, -1, tinyhashsize * sizeof(hash_positions[0]));

	__m128i vzero = _mm_setzero_si128();
	int bytemask = (0xff00 >> bitpos);
	int inverted_bitpos = 7 - bitpos;
	int nmodels = models.nmodels;
	ptrdiff_t pos_threshold = 0;
	for(int modeli = 0; modeli < nmodels; modeli++)
	{
		int weight = models[modeli].weight;
		__m128i vweight = _mm_setr_epi32(weight, 0, 0, 0);
		unsigned char w = (unsigned char)models[modeli].mask; 

		unsigned char maskbytes[16] = {};
		for(int i = 0; i < 8; i++) {
			maskbytes[i] = ((w >> i) & 1) * 0xff;
		}
		maskbytes[8] = bytemask;
		__m128i mask = _mm_loadu_si128((__m128i*)maskbytes);

		__m128i next_masked_contextdata;
		unsigned int next_tinyhash;

		next_masked_contextdata = _mm_and_si128(_mm_loadu_si128((__m128i *)(data - MAX_CONTEXT_LENGTH)), mask);

		next_tinyhash = Hash(next_masked_contextdata) & tinyhashmask;
		
		for(int pos = 0; pos < size; pos++) {
			int bit = (data[pos] >> inverted_bitpos) & 1;

			__m128i masked_contextdata = next_masked_contextdata;
			size_t tinyhash = next_tinyhash;
			next_masked_contextdata = _mm_and_si128(_mm_loadu_si128((__m128i *)(data + pos + 1 - MAX_CONTEXT_LENGTH)), mask);
			next_tinyhash = Hash(next_masked_contextdata) & tinyhashmask;

			while(true)
			{
				ptrdiff_t candidate_pos = hash_positions[tinyhash] - pos_threshold;
				if(candidate_pos < 0)
				{
					hash_positions[tinyhash] = int(pos + pos_threshold);
					hash_counter_states[tinyhash] = bit;	// counter_states is arranges such that (1,0) is 0 and (0,1) is 1.
					break;
				}
				
				if(_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(_mm_loadu_si128((__m128i *)&data[candidate_pos - MAX_CONTEXT_LENGTH]), mask), masked_contextdata)) == 0xFFFF)
				{
					CounterState& state = counter_states_ptr[hash_counter_states[tinyhash]];
					__m128i vsum = _mm_loadl_epi64((__m128i*)&sums[pos * 2]);
					vsum = _mm_add_epi32(vsum, _mm_sll_epi32(_mm_unpacklo_epi16(_mm_loadl_epi64((__m128i*)state.boosted_counters), vzero), vweight));
					_mm_storel_epi64((__m128i*)&sums[pos * 2], vsum);
					hash_counter_states[tinyhash] = state.next_state[bit];
					break;
				}
					
				tinyhash = (tinyhash + 1) & tinyhashmask;
			}
		}
		pos_threshold += size;
	}

	uint64_t totalsize = (8 + models.nmodels) * TABLE_BIT_PRECISION;
	for(int pos = 0; pos < size; pos++) {
		int bit = (data[pos] >> inverted_bitpos) & 1;
		totalsize += AritSize2(sums[pos * 2 + bit], sums[pos * 2 + !bit]);
	}
	
	delete[] hash_positions;
	delete[] hash_counter_states;

	data -= MAX_CONTEXT_LENGTH;
	delete[] data;
	delete[] sums;

	return (int) (totalsize / (TABLE_BIT_PRECISION / BIT_PRECISION));
}

CompressionStream::CompressionStream(unsigned char* data, int* sizefill, int maxCompressedSize, bool saturate) :
m_data(data), m_sizefill(sizefill), m_sizefillptr(sizefill), m_maxsize(maxCompressedSize), m_saturate(saturate)
{
	if(data != NULL) {
		memset(m_data, 0, m_maxsize);
		AritCodeInit(&m_aritstate, m_data);
	}
}

int CompressionStream::Close(void) {
	return (AritCodeEnd(&m_aritstate) + 7) / 8;
}
