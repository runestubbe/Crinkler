#pragma once
#ifndef _COMPRESSION_STATE_EVALUATOR_H_
#define _COMPRESSION_STATE_EVALUATOR_H_

#include "ModelList.h"

struct CounterPair {
	int p0, p1;
	int old_size;
};

struct Weights {
	unsigned char prob[2];
	unsigned int pos;
};

struct ModelPredictions {
	int nWeights;
	Weights* weights;
};

class CompressionStateEvaluator {
public:
	virtual ~CompressionStateEvaluator() {};
	virtual bool init(ModelPredictions* models, int length, int baseprob) = 0;
	virtual long long evaluate(const ModelList& models) = 0;
};

#endif
