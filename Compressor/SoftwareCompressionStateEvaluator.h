#pragma once
#ifndef _SOFTWARE_COMPRESSION_STATE_EVALUATOR_H_
#define _SOFTWARE_COMPRESSION_STATE_EVALUATOR_H_

#include "CompressionStateEvaluator.h"

class SoftwareCompressionStateEvaluator : public CompressionStateEvaluator {
	int						m_weights[256];
	ModelPredictions*		m_models;
	int						m_length;
	CounterPair*			m_sums;
	long long				m_compressedSize;
	int						m_baseprob;

	long long changeWeight(int modelIndex, int diffw);
public:
	SoftwareCompressionStateEvaluator();
	~SoftwareCompressionStateEvaluator();

	bool init(ModelPredictions* models, int length, int baseprob);
	long long evaluate(const ModelList& models);
};

#endif
