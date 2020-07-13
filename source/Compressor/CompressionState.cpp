#include "CompressionState.h"
#include <memory>
#include <ppl.h>

#include "ModelList.h"
#include "aritcode.h"
#include "model.h"
#include "compressor.h"

struct HashEntry {
	unsigned char mask;
	unsigned char bitnum;
	Weights w;
	const unsigned char* datapos;
};

static int previousPrime(int n) {
in:
	n = (n-2)|1;
	for (int i = 3 ; i*i < n ; i += 2) {
		if (n/i*i == n) goto in;
	}
	return n;
}

static HashEntry *findEntry(HashEntry *table, unsigned int hashsize, unsigned char mask, const unsigned char *data, int bitpos) {
	const unsigned char *datapos = &data[bitpos/8];
	unsigned char bitnum = (unsigned char)(bitpos & 7);
	for (unsigned int hash = ModelHash(data, bitpos, mask, HASH_MULTIPLIER) ;; hash = hash+1) {
		HashEntry *e = &table[hash % hashsize];
		if (e->datapos == 0) {
			e->mask = mask;
			e->bitnum = bitnum;
			e->datapos = datapos;
			return e;
		}
		if (e->mask == mask && e->bitnum == bitnum &&
			(datapos[0] & (0xFF00 >> bitnum)) == (e->datapos[0] & (0xFF00 >> bitnum))) {
				int all = 1;
				for (int i = 0 ; i < 8 ; i++) {
					if (((mask >> i) & 1) && datapos[i-8] != e->datapos[i-8]) {
						all = 0;
						break;
					}
				}
				if (all) return e;
			}
	}
}

void updateWeights(Weights *w, int bit, bool saturate) {
	if (!saturate || w->prob[bit] < 255) w->prob[bit] += 1;
	if (w->prob[!bit] > 1) w->prob[!bit] >>= 1;
}

ModelPredictions CompressionState::applyModel(const unsigned char* data, int bitlength, unsigned char mask) {
	int hashsize = previousPrime(bitlength*2);
	
	int maxPackages = (bitlength + PACKAGE_SIZE - 1) / PACKAGE_SIZE;
	int numPackages = 0;

	CompactPackage* packages = (CompactPackage*)_aligned_malloc(maxPackages * sizeof(CompactPackage), alignof(CompactPackage));
	int* packageOffsets = new int[maxPackages];
	HashEntry* hashtable = new HashEntry[hashsize];
	memset(hashtable, 0, hashsize*sizeof(HashEntry));
	
	__m128 logScale = _mm_set1_ps(m_logScale);
	for (int idx = 0 ; idx < maxPackages; idx++) {
		int bitpos_base = idx * PACKAGE_SIZE;
		
		bool package_needs_commit = false;
		for(int bitpos_offset = 0; bitpos_offset < PACKAGE_SIZE; bitpos_offset++)
		{
			float p_right = 0;
			float p_total = 0;
			if(bitpos_base + bitpos_offset < bitlength)
			{
				int bit = GetBit(data, bitpos_base + bitpos_offset);
				HashEntry *e = findEntry(hashtable, hashsize, mask, data, bitpos_base + bitpos_offset);
				int boost = (e->w.prob[0] == 0 || e->w.prob[1] == 0) ? 2 : 0;
				if(e->w.prob[0] || e->w.prob[1])
					package_needs_commit = true;

				p_right = (float)(e->w.prob[bit] << boost);
				p_total = (float)((e->w.prob[0] + e->w.prob[1]) << boost);
				updateWeights(&e->w, bit, m_saturate);
			}

			assert((*(int*)&p_right & 0xFFFF) == 0);
			assert((*(int*)&p_total & 0xFFFF) == 0);

			packages[numPackages].prob[bitpos_offset >> 2].m128i_u16[(bitpos_offset & 3)] = *(int*)&p_right >> 16;
			packages[numPackages].prob[bitpos_offset >> 2].m128i_u16[4 + (bitpos_offset & 3)] = *(int*)&p_total >> 16;
		}
		packageOffsets[numPackages] = idx;
		if(package_needs_commit)
			numPackages++;	// actually commit the package if 
	}

	delete[] hashtable;

	ModelPredictions mp;
	mp.numPackages = numPackages;
	mp.packageOffsets = packageOffsets;
	mp.packages = packages;
	return mp;
}

CompressionState::CompressionState(const unsigned char* data, int size, int baseprob, bool saturate, CompressionStateEvaluator* evaluator, char* context) :
	m_size(size*8), m_saturate(saturate), m_stateEvaluator(evaluator)
{
	//create temporary data buffer with heading zeros
	unsigned char* data2 = new unsigned char[size+MAX_CONTEXT_LENGTH];
	memcpy(data2, context, MAX_CONTEXT_LENGTH);
	memcpy(data2+MAX_CONTEXT_LENGTH, data, size);

	assert(baseprob >= 9);
	//m_logScale = pow(pow(2.0, -126.0) / baseprob, 1.0 / 16.0);
	//(*(int*)&m_logScale)++;
	m_logScale = 1.0f / 2048.0f;	// baseprob * logScale^16 >= FLT_MIN

	//apply models
#if USE_OPENMP
	#pragma omp parallel for
	for(int mask = 0; mask <= 0xff; mask++) {
		m_models[mask] = applyModel(data2+8, m_size, (unsigned char)mask);
	}
#else
	concurrency::parallel_for(0, 0x100, [&](int mask)
	{
		m_models[mask] = applyModel(data2+8, m_size, (unsigned char)mask);
	});
#endif
	delete[] data2;

	m_stateEvaluator->init(m_models, size*8, baseprob, m_logScale);
	m_compressedsize = BITPREC_TABLE*(long long)m_size;
}

CompressionState::~CompressionState() {
	for(int i = 0; i < 256; i++) {
		_aligned_free(m_models[i].packages);
		delete[] m_models[i].packageOffsets;
	}
}


int CompressionState::setModels(const ModelList& models) {
	m_compressedsize = m_stateEvaluator->evaluate(models);
	return (int) (m_compressedsize / (BITPREC_TABLE / BITPREC));
}

int CompressionState::getCompressedSize() const {
	return (int) (m_compressedsize / (BITPREC_TABLE / BITPREC));
}

int CompressionState::getSize() const {
	return m_size;
}
