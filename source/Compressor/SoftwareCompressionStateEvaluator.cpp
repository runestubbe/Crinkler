#include "SoftwareCompressionStateEvaluator.h"
#include <cstdlib>
#include <memory>
#include <ppl.h>

#include "aritcode.h"


SoftwareCompressionStateEvaluator::SoftwareCompressionStateEvaluator() :
	m_models(NULL), m_p0s(NULL), m_p1s(NULL), m_oldsizes(NULL)
{
	memset(m_weights, 0, sizeof(m_weights));
}

SoftwareCompressionStateEvaluator::~SoftwareCompressionStateEvaluator() {
	delete[] m_p0s;
	delete[] m_p1s;
	delete[] m_oldsizes;
}

bool SoftwareCompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprob)
{
	m_length = length;
	m_models = models;
	m_baseprob = baseprob;

	assert(length % 4 == 0);
	int numPackages = length / 4;

	m_p0s = new __m128[numPackages];
	m_p1s = new __m128[numPackages];
	m_oldsizes = new __m128i[numPackages];
	for(int i = 0; i < numPackages; i++) {
		m_p0s[i] = m_p1s[i] = _mm_set1_ps(baseprob);
		m_oldsizes[i] = _mm_set1_epi32(BITPREC_TABLE);
	}
	m_compressedSize = BITPREC_TABLE*length;

	m_logScale = pow(pow(2.0, -126.0) / baseprob, 1.0/16.0);
	(*(int*)&m_logScale)++;

	return true;
}

/*
assert(right_prob > 0);
assert(wrong_prob > 0);

int right_len, total_len;
int total_prob = right_prob + wrong_prob;
if(total_prob < BITPREC_TABLE) {
	return LogTable[total_prob] - LogTable[right_prob];
}*/

int SoftwareCompressionStateEvaluator::fastlog(int x)
{
	//int ref = int(log2(x)*BITPREC_TABLE + 0.5f);

	float f = x * m_logScale;
	f *= f;
	f *= f;
	f *= f;
	f *= f;
	return *(int*)&f >> (27 - BITPREC_TABLE_BITS);
}

int SoftwareCompressionStateEvaluator::AritSize_Test(int right_prob, int wrong_prob) {
	assert(right_prob > 0);
	assert(wrong_prob > 0);

	int total_prob = right_prob + wrong_prob;
#if 1
	int right_log = fastlog(right_prob);
	int total_log = fastlog(total_prob);
#else
	int right_log = int(log2(right_prob) * BITPREC_TABLE - BITPREC_TABLE_BITS * BITPREC_TABLE + 0.5f);
	int total_log = int(log2(total_prob) * BITPREC_TABLE - BITPREC_TABLE_BITS * BITPREC_TABLE + 0.5f);
#endif

	return total_log - right_log;
}

long long SoftwareCompressionStateEvaluator::changeWeight(int modelIndex, int diffw) {
	
	
	int numPackages = m_models[modelIndex].numPackages;
	int* packageOffsets = m_models[modelIndex].packageOffsets;
	Package* packages = m_models[modelIndex].packages;


	concurrency::combinable<long long> diffsize;

	const int BLOCK_SIZE = 32;
	int num_blocks = (numPackages + BLOCK_SIZE - 1) / BLOCK_SIZE;

	//for(int n = 0; n < length; n++)
	concurrency::parallel_for(0, num_blocks, [&](int block)
	{
		int start_idx = block * BLOCK_SIZE;
		int end_idx = std::min(start_idx + BLOCK_SIZE, numPackages);

		__m128 vlogScale = _mm_set1_ps(m_logScale);
		__m128 vdiffw = _mm_set1_ps(diffw);
		__m128i vdiffsize = _mm_setzero_si128();

		for(int i = start_idx; i < end_idx; i++)
		{
			int packageOffset = packageOffsets[i];
			Package* package = &packages[i];

			__m128 vmodel_p0 = package->p0;
			__m128 vmodel_p1 = package->p1;

			__m128 vtotal_p0 = m_p0s[packageOffset];
			__m128 vtotal_p1 = m_p1s[packageOffset];

			vtotal_p0 = _mm_add_ps(vtotal_p0, _mm_mul_ps(vmodel_p0, vdiffw));
			vtotal_p1 = _mm_add_ps(vtotal_p1, _mm_mul_ps(vmodel_p1, vdiffw));
			m_p0s[packageOffset] = vtotal_p0;
			m_p1s[packageOffset] = vtotal_p1;

			__m128i voldsize = m_oldsizes[packageOffset];

			__m128 vfr = _mm_mul_ps(vtotal_p0, vlogScale);
			__m128 vft = _mm_mul_ps(_mm_add_ps(vtotal_p0, vtotal_p1), vlogScale);

			vfr = _mm_mul_ps(vfr, vfr);	vfr = _mm_mul_ps(vfr, vfr);	vfr = _mm_mul_ps(vfr, vfr);	vfr = _mm_mul_ps(vfr, vfr);
			vft = _mm_mul_ps(vft, vft);	vft = _mm_mul_ps(vft, vft);	vft = _mm_mul_ps(vft, vft);	vft = _mm_mul_ps(vft, vft);

			__m128i vlog_fr = _mm_srli_epi32(_mm_castps_si128(vfr), 27 - BITPREC_TABLE_BITS);
			__m128i vlog_ft = _mm_srli_epi32(_mm_castps_si128(vft), 27 - BITPREC_TABLE_BITS);

			__m128i vnewsize = _mm_sub_epi32(vlog_ft, vlog_fr);
			m_oldsizes[packageOffset] = vnewsize;

			vdiffsize = _mm_add_epi32(vdiffsize, _mm_sub_epi32(vnewsize, voldsize));
		}

		diffsize.local() += vdiffsize.m128i_i32[0] + vdiffsize.m128i_i32[1] + vdiffsize.m128i_i32[2] + vdiffsize.m128i_i32[3];
	});

	return diffsize.combine(std::plus<long long>());
}

long long SoftwareCompressionStateEvaluator::evaluate(const ModelList& ml) {
	int newWeights[MAX_MODELS];
	memset(newWeights, 0, sizeof(newWeights));
	for(int i = 0; i < ml.nmodels; i++) {
		newWeights[ml[i].mask] = 1<<ml[i].weight;
	}

	for(int i = 0; i < MAX_MODELS; i++) {
		if(newWeights[i] != m_weights[i]) {
			long long diffsize = changeWeight(i, newWeights[i] - m_weights[i]);
			m_weights[i] = newWeights[i];
			m_compressedSize += diffsize;
		}
	
	}
	return m_compressedSize;
}
