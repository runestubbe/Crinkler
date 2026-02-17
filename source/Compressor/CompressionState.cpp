#include "CompressionState.h"
#include <memory>
#include <ppl.h>

#include "ModelList.h"
#include "AritCode.h"
#include "Model.h"
#include "Compressor.h"

struct HashEntry {
	unsigned int mask;
	unsigned char bitnum;
	Weights w;
	const unsigned char* datapos;
};

static int PreviousPrime(int n) {
in:
	n = (n-2)|1;
	for (int i = 3 ; i*i < n ; i += 2) {
		if (n/i*i == n) goto in;
	}
	return n;
}

static HashEntry *FindEntry(HashEntry *table, unsigned int hashsize, unsigned int mask, const unsigned char *data, int bitpos) {
	
	const unsigned char *datapos = &data[bitpos/8];
	unsigned char bitnum = (unsigned char)(bitpos & 7);

	for (unsigned int hash = ModelHash(data, bitpos, mask) ;; hash = hash+1) {
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
				for (int i = 0 ; i < 32 ; i++) {
					if (((mask >> i) & 1) && datapos[-i-1] != e->datapos[-i-1]) {
						all = 0;
						break;
					}
				}
				if (all) return e;
			}
	}
}

void UpdateWeights(Weights *w, int bit, bool saturate) {
	if (!saturate || w->prob[bit] < 255) w->prob[bit] += 1;
	if (w->prob[!bit] > 1) w->prob[!bit] >>= 1;
}

ModelPredictions CompressionState::ApplyModel(const unsigned char* data, int bitlength, unsigned int mask) {
	int hashsize = PreviousPrime(bitlength*2);
	
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
				HashEntry *e = FindEntry(hashtable, hashsize, mask, data, bitpos_base + bitpos_offset);
				int boost = (e->w.prob[0] == 0 || e->w.prob[1] == 0) ? 2 : 0;
				if(e->w.prob[0] || e->w.prob[1])
					package_needs_commit = true;

				p_right = (float)(e->w.prob[bit] << boost);
				p_total = (float)((e->w.prob[0] + e->w.prob[1]) << boost);
				UpdateWeights(&e->w, bit, m_saturate);
			}

			assert((*(int*)&p_right & 0xFFFF) == 0);
			assert((*(int*)&p_total & 0xFFFF) == 0);

			packages[numPackages].prob[bitpos_offset >> 2].m128i_u16[(bitpos_offset & 3)] = *(int*)&p_right >> 16;
			packages[numPackages].prob[bitpos_offset >> 2].m128i_u16[4 + (bitpos_offset & 3)] = *(int*)&p_total >> 16;
		}
		packageOffsets[numPackages] = idx;
		if(package_needs_commit)
			numPackages++;	// Actually commit the package if 
	}

	delete[] hashtable;

	ModelPredictions mp;
	mp.numPackages = numPackages;
	mp.packageOffsets = packageOffsets;
	mp.packages = packages;
	return mp;
}

CompressionState::CompressionState(const unsigned char* data, int size, int baseprob, bool saturate, CompressionStateEvaluator* evaluator, const unsigned char* context) :
	m_size(size*8), m_saturate(saturate), m_stateEvaluator(evaluator)
{
	m_models = new ModelPredictions[MAX_MODELS];

	// Create temporary data buffer with leading zeros
	unsigned char* data2 = new unsigned char[size+MAX_CONTEXT_LENGTH];
	memcpy(data2, context, MAX_CONTEXT_LENGTH);
	memcpy(data2+MAX_CONTEXT_LENGTH, data, size);

	assert(baseprob >= 9);
	m_logScale = 1.0f / 2048.0f;	// baseprob * logScale^16 >= FLT_MIN

	// Apply models
#if USE_OPENMP
	#pragma omp parallel for
	for(int i = 0; i <= MAX_MODELS; i++) {
		unsigned int mask = i;
		m_models[i] = ApplyModel(data2+MAX_CONTEXT_LENGTH, m_size, mask);
	}
#else
	concurrency::parallel_for(0, MAX_MODELS, [&](int i)
	{
		// 0xFF: 1361.85, 2493.04, 3960.19
		// 0x7F: 1362.52, 2539.08, 3974.84
		// 0xBF: 1363.99, 2506.17, 3965.37
		// 0xDF: 1364.27, 2499.97, 3967.06
		// 0xEF: 1375.16, 2503.53, 3971.17
		// 0xF7: 1370.03, 2536.03, 3986.07
		// 0xFB: 1377.27, 2514.48, 4013.48
		// 0xFD: 1404.10, 2583.03, 4268.61
		// 0xFE: 1528.14, 2622.68, 5209.78

		// masked (based on bit 7): 1362.67, 2510.51, 3961.13
		// masked (based on bit 6): 1361.74, 2488.61, 3958.26
		unsigned int mask = i;
		m_models[i] = ApplyModel(data2+MAX_CONTEXT_LENGTH, m_size, mask);
	});
#endif
	delete[] data2;

	m_stateEvaluator->Init(m_models, size*8, baseprob, m_logScale);
	m_compressedsize = TABLE_BIT_PRECISION*(long long)m_size;
}

CompressionState::~CompressionState() {
	for(int i = 0; i < MAX_MODELS; i++) {
		_aligned_free(m_models[i].packages);
		delete[] m_models[i].packageOffsets;
	}
	delete[] m_models;
}

int CompressionState::SetModels(const ModelList4k& models) {
	m_compressedsize = m_stateEvaluator->Evaluate(models);
	return (int) (m_compressedsize / (TABLE_BIT_PRECISION / BIT_PRECISION));
}
