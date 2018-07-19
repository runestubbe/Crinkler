#pragma once
#ifndef _COMPRESSION_STATE_EVALUATOR_H_
#define _COMPRESSION_STATE_EVALUATOR_H_

#include "Compressor.h"
#include "ModelList.h"
#include <emmintrin.h>

struct CounterPair {
	float p0, p1;
	int old_size;
};

struct Weights {
	unsigned char prob[2];
	unsigned int pos;
};

struct CompactPackage
{
	__m128i prob[NUM_PACKAGE_VECTORS];	//0..63: right, 64..127: total
};

struct Package
{
	__m128 prob[NUM_PACKAGE_VECTORS][2];	//right, total
};

struct ModelPredictions {
	int numPackages;
	CompactPackage* packages;
	int* packageOffsets;
};

class CompressionStateEvaluator {
public:
	virtual ~CompressionStateEvaluator() {};
	virtual bool init(ModelPredictions* models, int length, int baseprob, float logScale) = 0;
	virtual long long evaluate(const ModelList& models) = 0;
};

#endif
