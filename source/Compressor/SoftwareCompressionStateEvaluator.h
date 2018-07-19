#pragma once
#ifndef _SOFTWARE_COMPRESSION_STATE_EVALUATOR_H_
#define _SOFTWARE_COMPRESSION_STATE_EVALUATOR_H_

#include "CompressionStateEvaluator.h"
#include <emmintrin.h>

class SoftwareCompressionStateEvaluator : public CompressionStateEvaluator {
	int						m_weights[256];
	ModelPredictions*		m_models;
	
	int						m_length;
	int						m_numPackages;
	Package*				m_packages;
	unsigned int*			m_packageSizes;

	long long				m_compressedSize;
	int						m_baseprob;
	float					m_logScale;

	long long changeWeight(int modelIndex, int diffw);
	int fastlog(int x);
	int AritSize_Test(int right_prob, int wrong_prob);
public:
	SoftwareCompressionStateEvaluator();
	~SoftwareCompressionStateEvaluator();

	bool init(ModelPredictions* models, int length, int baseprob, float logScale);
	long long evaluate(const ModelList& models);
};

#endif
