#include "SoftwareCompressionStateEvaluator.h"
#include <cstdlib>
#include <memory>
#include <ppl.h>

#include "aritcode.h"

#define IACA_VC64_START __writegsbyte(111, 111);
#define IACA_VC64_END   __writegsbyte(222, 222);

SoftwareCompressionStateEvaluator::SoftwareCompressionStateEvaluator() :
	m_models(NULL), m_packages(NULL), m_packageSizes(NULL)
{
	memset(m_weights, 0, sizeof(m_weights));
}

SoftwareCompressionStateEvaluator::~SoftwareCompressionStateEvaluator() {
	delete[] m_packages;
	delete[] m_packageSizes;
}

bool SoftwareCompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprob, float logScale)
{
	m_length = length;
	m_models = models;
	m_baseprob = baseprob;

	int numPackages = (length + PACKAGE_SIZE - 1) / PACKAGE_SIZE;
	m_logScale = logScale;
	m_packages = new Package[numPackages];
	m_packageSizes = new unsigned int[numPackages];
	for(int i = 0; i < numPackages; i++) {
		for(int j = 0; j < NUM_PACKAGE_VECTORS; j++)
		{
			m_packages[i].prob[j][0] = _mm_set1_ps(baseprob * logScale);
			m_packages[i].prob[j][1] = _mm_set1_ps(baseprob * 2 * logScale);
		}
		m_packageSizes[i] = std::min(length - i * PACKAGE_SIZE, PACKAGE_SIZE) * BITPREC_TABLE;
	}
	m_compressedSize = BITPREC_TABLE*length;

	return true;
}

long long SoftwareCompressionStateEvaluator::changeWeight(int modelIndex, int diffw) {
	concurrency::combinable<long long> diffsize;

	int numPackages = m_models[modelIndex].numPackages;
	const int PACKAGES_PER_JOB = 64;
	int num_jobs = (numPackages + PACKAGES_PER_JOB - 1) / PACKAGES_PER_JOB;

	//for(int n = 0; n < length; n++)
	concurrency::parallel_for(0, num_jobs, [&](int job)
	{
		int package_idx_base = job * PACKAGES_PER_JOB;
		int* packageOffsets = m_models[modelIndex].packageOffsets;
		
		__m128 vlogScale = _mm_set1_ps(m_logScale);
		__m128 vdiffw = _mm_set1_ps(diffw * m_logScale);
		__m128i zero = _mm_setzero_si128();
		
		Package* sum_packages = m_packages;
		CompactPackage* model_packages = m_models[modelIndex].packages;
		unsigned int* packageSizes = m_packageSizes;

		int diffsize2 = 0;
		for(int i = 0; i < PACKAGES_PER_JOB && package_idx_base + i < numPackages; i++)
		{
			IACA_VC64_START
			int package_idx = package_idx_base + i;
			
			int packageOffset = packageOffsets[package_idx];
			
			Package* sum_package = &sum_packages[packageOffset];
			CompactPackage* model_package = &model_packages[package_idx];
			
			__m128 vprod_right = _mm_set1_ps(1.0f);
			__m128 vprod_total  = _mm_set1_ps(1.0f);
			__m128 vsum_p_right, vsum_p_total;
			__m128i packed;

#if 0
			__m128i packed_right, packed_wrong;
			__m128i right16_low = _mm_unpacklo_epi8(packed_right, zero);
			__m128i right16_high = _mm_unpackhi_epi8(packed_right, zero);
			__m128i wrong16_low = _mm_unpacklo_epi8(packed_wrong, zero);
			__m128i wrong16_high = _mm_unpackhi_epi8(packed_wrong, zero);


			//  8bit, 2 iterations: 
			// 16bit, 2 iterations: 2x load, 4x cvt, 4x unpack
#endif

#define DO(_IDX) \
			vsum_p_right = sum_package->prob[_IDX][0]; \
			vsum_p_total = sum_package->prob[_IDX][1]; \
			packed = model_package->prob[_IDX]; \
			vsum_p_right = _mm_add_ps(vsum_p_right, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(packed, zero)), vdiffw)); \
			vsum_p_total = _mm_add_ps(vsum_p_total, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(packed, zero)), vdiffw)); \
			sum_package->prob[_IDX][0] = vsum_p_right; \
			sum_package->prob[_IDX][1] = vsum_p_total; \
			vprod_right = _mm_mul_ps(vprod_right, vsum_p_right); \
			vprod_total = _mm_mul_ps(vprod_total, vsum_p_total);

			DO(0) DO(1) DO(2) DO(3)
			DO(4) DO(5) DO(6) DO(7)
			DO(8) DO(9) DO(10) DO(11)
			DO(12) DO(13) DO(14) DO(15)
			
			/*
			for(int j = 0; j < NUM_PACKAGE_VECTORS; j++)
			{
				vsum_p_right = sum_package->prob[j][0];
				vsum_p_total = sum_package->prob[j][1];
				__m128i packed = model_package->prob[j];
				vsum_p_right = _mm_add_ps(vsum_p_right, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(packed, zero)), vdiffw));
				vsum_p_total = _mm_add_ps(vsum_p_total, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(packed, zero)), vdiffw));
				sum_package->prob[j][0] = vsum_p_right;
				sum_package->prob[j][1] = vsum_p_total;
				vprod_right = _mm_mul_ps(vprod_right, vsum_p_right);
				vprod_total = _mm_mul_ps(vprod_total, vsum_p_total);
			}
			*/

			__m128i vnewsize = _mm_srli_epi32(_mm_sub_epi32(_mm_castps_si128(vprod_total), _mm_castps_si128(vprod_right)), 27 - BITPREC_TABLE_BITS - LOG2_NUM_PACKAGE_VECTORS);
			vnewsize = _mm_add_epi32(vnewsize, _mm_shuffle_epi32(vnewsize, _MM_SHUFFLE(1, 0, 3, 2)));
			vnewsize = _mm_add_epi32(vnewsize, _mm_shuffle_epi32(vnewsize, _MM_SHUFFLE(2, 3, 0, 1)));
			int newsize = _mm_cvtsi128_si32(vnewsize);
			
			//newsize = newsize2;
			int oldsize = packageSizes[packageOffset];
			packageSizes[packageOffset] = newsize;
			diffsize2 += newsize - oldsize;
			IACA_VC64_END
		}

		diffsize.local() += diffsize2;
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
