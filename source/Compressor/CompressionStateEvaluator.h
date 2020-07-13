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
	int						m_weights[256];
	ModelPredictions* m_models;

	int						m_length;
	int						m_numPackages;
	Package* m_packages;
	unsigned int* m_packageSizes;

	long long				m_compressedSize;
	int						m_baseprob;
	float					m_logScale;

	long long changeWeight(int modelIndex, int diffw);
	int fastlog(int x);
	int AritSize_Test(int right_prob, int wrong_prob);
public:
	CompressionStateEvaluator();
	~CompressionStateEvaluator();

	bool init(ModelPredictions* models, int length, int baseprob, float logScale);
	long long evaluate(const ModelList& models);
};

#endif
