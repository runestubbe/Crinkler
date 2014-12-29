#include "CompressionState.h"
#include <memory>
#include <ppl.h>

#include "ModelList.h"
#include "aritcode.h"
#include "model.h"


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

void updateWeights(Weights *w, int bit) {
	w->prob[bit] += 1;
	if (w->prob[!bit] > 1) w->prob[!bit] >>= 1;
}

ModelPredictions CompressionState::applyModel(const unsigned char* data, int bitlength, unsigned char mask) {
	int hashsize = previousPrime(bitlength*2);

	Weights* weights = new Weights[bitlength];
	HashEntry* hashtable = new HashEntry[hashsize];
	memset(hashtable, 0, hashsize*sizeof(HashEntry));
	Weights* w = weights;
	for (int bitpos = 0 ; bitpos < bitlength ; bitpos++) {
		HashEntry *e = findEntry(hashtable, hashsize, mask, data, bitpos);
		int bit = GetBit(data, bitpos);
		if(e->w.prob[0] || e->w.prob[1]) {
			int boost = !(e->w.prob[0] && e->w.prob[1]);
			w->pos = bitpos | (boost << 31);
			w->prob[0] = e->w.prob[bit];
			w->prob[1] = e->w.prob[!bit];
			w++;
		}

		updateWeights(&e->w, bit);
	}

	delete[] hashtable;

	ModelPredictions mp;
	mp.nWeights = w - weights;
	mp.weights = weights;
	return mp;
}

CompressionState::CompressionState(const unsigned char* data, int size, int baseprob, CompressionStateEvaluator* evaluator) :
	m_size(size*8), m_stateEvaluator(evaluator)
{	
	//create temporary data buffer with heading zeros
	unsigned char* data2 = new unsigned char[size+8];
	memset(data2, 0, 8);
	memcpy(data2+8, data, size);

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
		delete[] m_models[i].weights;
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
