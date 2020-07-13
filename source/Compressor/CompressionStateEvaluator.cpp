#include "CompressionStateEvaluator.h"
#include <cstdlib>
#include <memory>
#include <ppl.h>

#include "aritcode.h"

#define IACA_VC64_START __writegsbyte(111, 111);
#define IACA_VC64_END   __writegsbyte(222, 222);

#define USE_POLY3
//#define USE_POLY4

#define EXTRA_BITS 0

CompressionStateEvaluator::CompressionStateEvaluator() :
	m_models(NULL), m_packages(NULL), m_packageSizes(NULL)
{
	memset(m_weights, 0, sizeof(m_weights));
}

CompressionStateEvaluator::~CompressionStateEvaluator() {
	_aligned_free(m_packages);
	delete[] m_packageSizes;
}

bool CompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprob, float logScale)
{
	m_length = length;
	m_models = models;
	m_baseprob = baseprob;

	int numPackages = (length + PACKAGE_SIZE - 1) / PACKAGE_SIZE;
	m_logScale = logScale;
	m_packages = (Package*)_aligned_malloc(numPackages * sizeof(Package), alignof(Package));
	m_packageSizes = new unsigned int[numPackages];
	for(int i = 0; i < numPackages; i++) {
		for(int j = 0; j < NUM_PACKAGE_VECTORS; j++)
		{
			m_packages[i].prob[j][0] = _mm_set1_ps(baseprob * logScale);
			if(i * PACKAGE_SIZE + j * 4 < length)
				m_packages[i].prob[j][1] = _mm_set1_ps(baseprob * 2 * logScale);
			else
				m_packages[i].prob[j][1] = _mm_set1_ps(baseprob * logScale);	// right / total = 1.0
		}
		m_packageSizes[i] = std::min(length - i * PACKAGE_SIZE, PACKAGE_SIZE) * (BITPREC_TABLE << EXTRA_BITS);
	}
	m_compressedSize = (long long)length << BITPREC_TABLE_BITS;

	return true;
}

long long CompressionStateEvaluator::changeWeight(int modelIndex, int diffw) {
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
		__m128i vzero = _mm_setzero_si128();
		__m128i vmantissa_mask = _mm_set1_epi32(0x7fffff);
		__m128i vffff0000 = _mm_set1_epi32(0xFFFF0000);

		__m128 vone = _mm_set1_ps(1.0f);
		__m128 vhalf = _mm_set1_ps(0.5f);

#if defined(USE_POLY4)
		//-0.08213064886366, 0.32118884789690, -0.67778393289462, 
		__m128 vc0 = _mm_set1_ps(1.43872573386137f);
		__m128 vc1 = _mm_set1_ps(-0.67778393289462f);
		__m128 vc2 = _mm_set1_ps(0.32118884789690f);
		__m128 vc3 = _mm_set1_ps(-0.08213064886366f);
#elif defined(USE_POLY3)
		__m128 vc0 = _mm_set1_ps(1.42286530448213f);
		__m128 vc1 = _mm_set1_ps(-0.58208536795165f);
		__m128 vc2 = _mm_set1_ps(0.15922006346951f);
#endif
		__m128 vbitprec_scale = _mm_set1_ps(BITPREC_TABLE << EXTRA_BITS);

		
		Package* sum_packages = m_packages;
		CompactPackage* model_packages = m_models[modelIndex].packages;
		unsigned int* packageSizes = m_packageSizes;

		int64_t diffsize2 = 0;
		for(int i = 0; i < PACKAGES_PER_JOB && package_idx_base + i < numPackages; i++)
		{
			//IACA_VC64_START
			int package_idx = package_idx_base + i;
			
			int packageOffset = packageOffsets[package_idx];
			
			Package* sum_package = &sum_packages[packageOffset];
			CompactPackage* model_package = &model_packages[package_idx];
			
			__m128 vprod_right = vone;
			__m128 vprod_total  = vone;
			__m128 vsum_p_right, vsum_p_total;
			__m128i packed;


#define DO(_IDX) \
			vsum_p_right = sum_package->prob[_IDX][0]; \
			vsum_p_total = sum_package->prob[_IDX][1]; \
			packed = model_package->prob[_IDX]; \
			vsum_p_right = _mm_add_ps(vsum_p_right, _mm_mul_ps(_mm_castsi128_ps(_mm_unpacklo_epi16(vzero, packed)), vdiffw)); \
			vsum_p_total = _mm_add_ps(vsum_p_total, _mm_mul_ps(_mm_castsi128_ps(_mm_unpackhi_epi16(vzero, packed)), vdiffw)); \
			assert(vsum_p_total.m128_f32[0] < 16777216 * m_logScale); \
			assert(vsum_p_total.m128_f32[1] < 16777216 * m_logScale); \
			assert(vsum_p_total.m128_f32[2] < 16777216 * m_logScale); \
			assert(vsum_p_total.m128_f32[3] < 16777216 * m_logScale); \
			sum_package->prob[_IDX][0] = vsum_p_right; \
			sum_package->prob[_IDX][1] = vsum_p_total; \
			vprod_right = _mm_mul_ps(vprod_right, vsum_p_right); \
			vprod_total = _mm_mul_ps(vprod_total, vsum_p_total);
	
			DO(0) DO(1) DO(2) DO(3);
			DO(4) DO(5) DO(6) DO(7);
			DO(8) DO(9) DO(10) DO(11);
			DO(12) DO(13) DO(14) DO(15);
#undef DO

			__m128i viprod_right = _mm_castps_si128(vprod_right);
			__m128i viprod_total = _mm_castps_si128(vprod_total);
			__m128i viprod_right_exponent = _mm_srli_epi32(viprod_right, 23);
			__m128i viprod_total_exponent = _mm_srli_epi32(viprod_total, 23);
			__m128 vright_log = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(viprod_right, vmantissa_mask), _mm_castps_si128(vone)));
			__m128 vtotal_log = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(viprod_total, vmantissa_mask), _mm_castps_si128(vone)));


			
#if defined(USE_POLY4)
			// log2(x) approximation (a*(x-1)^2 + b*(x-1) + (1-a-b))*x
			// guaranteed to be exact at the endpoints x=1 and x=2
			vright_log = _mm_sub_ps(vright_log, vone);
			vright_log = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(vc3, vright_log), vc2), vright_log), vc1), vright_log), vc0), vright_log);

			vtotal_log = _mm_sub_ps(vtotal_log, vone);
			vtotal_log = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(vc3, vtotal_log), vc2), vtotal_log), vc1), vtotal_log), vc0), vtotal_log);

			__m128i vifrac_bits = _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(vtotal_log, vright_log), vbitprec_scale));
			__m128i vnewsize = _mm_add_epi32(_mm_slli_epi32(_mm_sub_epi32(viprod_total_exponent, viprod_right_exponent), BITPREC_TABLE_BITS + EXTRA_BITS), vifrac_bits);
#elif defined(USE_POLY3)
			// log2(x) approximation (a*(x-1)^2 + b*(x-1) + (1-a-b))*x
			// guaranteed to be exact at the endpoints x=1 and x=2
			vright_log = _mm_sub_ps(vright_log, vone);
			vright_log = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(vc2, vright_log), vc1), vright_log), vc0), vright_log);

			vtotal_log = _mm_sub_ps(vtotal_log, vone);
			vtotal_log = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(vc2, vtotal_log), vc1), vtotal_log), vc0), vtotal_log);

			__m128i vifrac_bits = _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(vtotal_log, vright_log), vbitprec_scale));
			__m128i vnewsize = _mm_add_epi32(_mm_slli_epi32(_mm_sub_epi32(viprod_total_exponent, viprod_right_exponent), BITPREC_TABLE_BITS + EXTRA_BITS), vifrac_bits);
#else
			vright_log = _mm_mul_ps(vright_log, vright_log);	vright_log = _mm_mul_ps(vright_log, vright_log);	vright_log = _mm_mul_ps(vright_log, vright_log);	vright_log = _mm_mul_ps(vright_log, vright_log);
			vtotal_log = _mm_mul_ps(vtotal_log, vtotal_log);	vtotal_log = _mm_mul_ps(vtotal_log, vtotal_log);	vtotal_log = _mm_mul_ps(vtotal_log, vtotal_log);	vtotal_log = _mm_mul_ps(vtotal_log, vtotal_log);
			
			__m128i vnewsize = _mm_sub_epi32(_mm_castps_si128(vtotal_log), _mm_castps_si128(vright_log));
			vnewsize = _mm_srai_epi32(vnewsize, 23 - BITPREC_TABLE_BITS + 4);
			vnewsize = _mm_add_epi32(vnewsize, _mm_slli_epi32(_mm_sub_epi32(viprod_total_exponent, viprod_right_exponent), BITPREC_TABLE_BITS));
#endif

			vnewsize = _mm_add_epi32(vnewsize, _mm_shuffle_epi32(vnewsize, _MM_SHUFFLE(1, 0, 3, 2)));
			vnewsize = _mm_add_epi32(vnewsize, _mm_shuffle_epi32(vnewsize, _MM_SHUFFLE(2, 3, 0, 1)));
			int newsize = _mm_cvtsi128_si32(vnewsize);


			int oldsize = packageSizes[packageOffset];
			packageSizes[packageOffset] = newsize;
			diffsize2 += newsize - oldsize;
			//IACA_VC64_END
		}

		diffsize.local() += diffsize2 / (1 << EXTRA_BITS);
	});

	return diffsize.combine(std::plus<long long>());
}

long long CompressionStateEvaluator::evaluate(const ModelList& ml) {
	int newWeights[MAX_MODELS];
	memset(newWeights, 0, sizeof(newWeights));
	for(int i = 0; i < ml.nmodels; i++) {
		newWeights[ml[i].mask] = 1<<ml[i].weight;
	}

	for(int i = 0; i < MAX_MODELS; i++) {
		if(newWeights[i] != m_weights[i]) {
			long long diffsize = changeWeight(i, newWeights[i] - m_weights[i]);
			if(m_weights[i] == 0)
				m_compressedSize += 8 * BITPREC_TABLE;
			else if(newWeights[i] == 0)
				m_compressedSize -= 8 * BITPREC_TABLE;
			m_weights[i] = newWeights[i];
			m_compressedSize += diffsize;
		}
	
	}
	return m_compressedSize;	// compressed size including model cost
}
