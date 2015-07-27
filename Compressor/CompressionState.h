#pragma once
#ifndef _COMPRESSION_STATE_
#define _COMPRESSION_STATE_

#include "CompressionStateEvaluator.h"

class CompressionState {
	int					m_size;
	bool				m_saturate;
	ModelPredictions	m_models[256];
	long long			m_compressedsize;
	CompressionStateEvaluator* m_stateEvaluator;
	ModelPredictions	applyModel(const unsigned char* data, int bitlength, unsigned char mask);
public:
	CompressionState(const unsigned char* data, int size, int baseprob, bool saturate, CompressionStateEvaluator* evaluator, char* context);
	~CompressionState();
	
	int setModels(const ModelList& models);

	int getCompressedSize() const;
	int getSize() const;
};
#endif
