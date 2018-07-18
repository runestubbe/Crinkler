#pragma once
#ifndef _COMPRESSION_STATE_EVALUATOR_H_
#define _COMPRESSION_STATE_EVALUATOR_H_

#include "ModelList.h"
#include <xmmintrin.h>

struct CounterPair {
	float p0, p1;
	int old_size;
};

struct Weights {
	unsigned char prob[2];
	unsigned int pos;
};

struct Package
{
	__m128 p_right, p_total;	// pre-boosted counts
};

struct ModelPredictions {
	int numPackages;
	Package* packages;
	int* packageOffsets;
};

class CompressionStateEvaluator {
public:
	virtual ~CompressionStateEvaluator() {};
	virtual bool init(ModelPredictions* models, int length, int baseprob, float logScale) = 0;
	virtual long long evaluate(const ModelList& models) = 0;
};

#endif
