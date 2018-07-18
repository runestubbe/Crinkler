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
	const byte *datapos;
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
	assert(bitlength % 4 == 0);
	
	int maxPackages = bitlength / 4;
	int numPackages = 0;

	Package* packages = new Package[maxPackages];
	int* packageOffsets = new int[maxPackages];
	HashEntry* hashtable = new HashEntry[hashsize];
	memset(hashtable, 0, hashsize*sizeof(HashEntry));
	
	for (int idx = 0 ; idx < maxPackages; idx++) {
		int bitpos = (idx * 4);
		
		int b0 = GetBit(data, bitpos + 0);
		int b1 = GetBit(data, bitpos + 1);
		int b2 = GetBit(data, bitpos + 2);
		int b3 = GetBit(data, bitpos + 3);
		
		HashEntry *e0 = findEntry(hashtable, hashsize, mask, data, bitpos + 0);
		HashEntry *e1 = findEntry(hashtable, hashsize, mask, data, bitpos + 1);
		HashEntry *e2 = findEntry(hashtable, hashsize, mask, data, bitpos + 2);
		HashEntry *e3 = findEntry(hashtable, hashsize, mask, data, bitpos + 3);

		assert(e0 != e1 && e0 != e2 && e0 != e3 && e1 != e2 && e1 != e3 && e2 != e3);
		
		if(e0->w.prob[0] || e0->w.prob[1] || e1->w.prob[0] || e1->w.prob[1] || e2->w.prob[0] || e2->w.prob[1] || e3->w.prob[0] || e3->w.prob[1])	// we have to test all of them as e0 might have overflowed back to (0,0)
		{
			int boost0 = (e0->w.prob[0] == 0 || e0->w.prob[1] == 0) ? 2 : 0;
			int boost1 = (e1->w.prob[0] == 0 || e1->w.prob[1] == 0) ? 2 : 0;
			int boost2 = (e2->w.prob[0] == 0 || e2->w.prob[1] == 0) ? 2 : 0;
			int boost3 = (e3->w.prob[0] == 0 || e3->w.prob[1] == 0) ? 2 : 0;
			
			packages[numPackages].p0 = _mm_setr_ps(e0->w.prob[b0] << boost0, e1->w.prob[b1] << boost1, e2->w.prob[b2] << boost2, e3->w.prob[b3] << boost3);
			packages[numPackages].p1 = _mm_setr_ps(e0->w.prob[!b0] << boost0, e1->w.prob[!b1] << boost1, e2->w.prob[!b2] << boost2, e3->w.prob[!b3] << boost3);
			packageOffsets[numPackages] = idx;
			numPackages++;
		}

		updateWeights(&e0->w, b0, m_saturate);
		updateWeights(&e1->w, b1, m_saturate);
		updateWeights(&e2->w, b2, m_saturate);
		updateWeights(&e3->w, b3, m_saturate);
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
	m_stateEvaluator->init(m_models, size*8, baseprob);
	m_compressedsize = BITPREC_TABLE*(long long)m_size;
}

CompressionState::~CompressionState() {
	for(int i = 0; i < 256; i++) {
		delete[] m_models[i].packages;
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
