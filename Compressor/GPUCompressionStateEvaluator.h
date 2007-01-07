#pragma once
#ifndef _GPU_COMPRESSION_STATE_EVALUATOR_H
#define _GPU_COMPRESSION_STATE_EVALUATOR_H

#include "CompressionStateEvaluator.h"

class GPUCompressionStateEvaluator : public CompressionStateEvaluator {
public:
	GPUCompressionStateEvaluator();
	~GPUCompressionStateEvaluator();

	bool init(ModelPredictions* models, int length, int baseprobs[8]);
	long long evaluate(const ModelList& models);
};

#endif