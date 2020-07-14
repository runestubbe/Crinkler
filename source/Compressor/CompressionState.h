#pragma once
#ifndef _COMPRESSION_STATE_
#define _COMPRESSION_STATE_

#include "CompressionStateEvaluator.h"

class CompressionState {
	int							m_size;
	bool						m_saturate;
	ModelPredictions			m_models[256];
	long long					m_compressedsize;
	CompressionStateEvaluator*	m_stateEvaluator;
	float						m_logScale;

	ModelPredictions			ApplyModel(const unsigned char* data, int bitlength, unsigned char mask);
public:
	CompressionState(const unsigned char* data, int size, int baseprob, bool saturate, CompressionStateEvaluator* evaluator, char* context);
	~CompressionState();
	
	int SetModels(const ModelList& models);

	int GetCompressedSize() const;
	int GetSize() const;
};
#endif
