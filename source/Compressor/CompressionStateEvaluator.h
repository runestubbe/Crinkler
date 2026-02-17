#pragma once
#ifndef _COMPRESSION_STATE_EVALUATOR_H_
#define _COMPRESSION_STATE_EVALUATOR_H_

#include "Compressor.h"
#include "ModelList.h"

#include <emmintrin.h>

static const int LOG2_NUM_PACKAGE_VECTORS	= 4;
static const int NUM_PACKAGE_VECTORS		= 1 << LOG2_NUM_PACKAGE_VECTORS;
static const int PACKAGE_SIZE				= NUM_PACKAGE_VECTORS * 4;

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
	__m128i prob[NUM_PACKAGE_VECTORS];	// 0..63: right, 64..127: total
};

struct Package
{
	__m128 prob[NUM_PACKAGE_VECTORS][2];	// right, total
};

struct ModelPredictions {
	int numPackages;
	CompactPackage* packages;
	int* packageOffsets;
};

class CompressionStateEvaluator {
	int*				m_weights;
	ModelPredictions*	m_models;

	int					m_length;
	int					m_numPackages;
	Package*			m_packages;
	unsigned int*		m_packageSizes;

	long long			m_compressedSize;
	int					m_baseprob;
	float				m_logScale;

	long long			ChangeWeight(int modelIndex, int diffw);
public:
	CompressionStateEvaluator();
	~CompressionStateEvaluator();

	bool		Init(ModelPredictions* models, int length, int baseprob, float logScale);
	long long	Evaluate(const ModelList4k& models);
};

#endif
