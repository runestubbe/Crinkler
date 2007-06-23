#include "SoftwareCompressionStateEvaluator.h"
#include <cstdlib>
#include <memory>

#include "aritcode.h"


SoftwareCompressionStateEvaluator::SoftwareCompressionStateEvaluator() :
	m_models(NULL), m_sums(NULL)
{
	memset(m_weights, 0, sizeof(m_weights));
}

SoftwareCompressionStateEvaluator::~SoftwareCompressionStateEvaluator() {
	delete[] m_sums;
}

bool SoftwareCompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprobs[8])
{
	m_length = length;
	m_models = models;
	memcpy(m_baseprobs, baseprobs, sizeof(baseprobs));

	m_sums = new CounterPair[length];
	for(int i = 0; i < length; i++) {
		m_sums[i].p0 = m_sums[i].p1 = baseprobs[i&7];
		m_sums[i].old_size = BITPREC_TABLE;
	}
	m_compressedSize = BITPREC_TABLE*length;
	return true;
}

long long SoftwareCompressionStateEvaluator::changeWeight(int modelIndex, int diffw) {
	long long diffsize = 0;
	Weights* model = m_models[modelIndex].weights;
	int length = m_models[modelIndex].nWeights;

	#pragma omp parallel for reduction(+:diffsize)
	for(int n = 0; n < length; n++) {
		int i = model[n].pos & 0x7FFFFFFF;
		int boost = (model[n].pos >> 30) & 0x2;

		int oldsize = m_sums[i].old_size;
		m_sums[i].p0 += (model[n].prob[0] * diffw)<<boost;
		m_sums[i].p1 += (model[n].prob[1] * diffw)<<boost;
		int newsize = AritSize2(m_sums[i].p0, m_sums[i].p1);
		m_sums[i].old_size = newsize;
		diffsize += (newsize - oldsize);
	}

	return diffsize;
}

long long SoftwareCompressionStateEvaluator::evaluate(const ModelList& ml) {
	int newWeights[MAX_MODELS];
	memset(newWeights, 0, sizeof(newWeights));
	for(int i = 0; i < ml.nmodels; i++) {
		newWeights[ml[i].mask] = 1<<ml[i].weight;
	}

	for(int i = 0; i < MAX_MODELS; i++) {
		if(newWeights[i] != m_weights[i]) {
			long long diffsize = changeWeight(i, newWeights[i] - m_weights[i]);
			m_weights[i] = newWeights[i];
			m_compressedSize += diffsize;
		}
	
	}
	return m_compressedSize;
}
