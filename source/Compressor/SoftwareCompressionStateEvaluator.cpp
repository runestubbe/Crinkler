#include "SoftwareCompressionStateEvaluator.h"
#include <cstdlib>
#include <memory>
#include <ppl.h>

#include "aritcode.h"

#define IACA_VC64_START __writegsbyte(111, 111);
#define IACA_VC64_END   __writegsbyte(222, 222);

SoftwareCompressionStateEvaluator::SoftwareCompressionStateEvaluator() :
	m_models(NULL), m_p_rights(NULL), m_p_totals(NULL), m_oldsizes(NULL)
{
	memset(m_weights, 0, sizeof(m_weights));
}

SoftwareCompressionStateEvaluator::~SoftwareCompressionStateEvaluator() {
	delete[] m_p_rights;
	delete[] m_p_totals;
	delete[] m_oldsizes;
}

bool SoftwareCompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprob, float logScale)
{
	m_length = length;
	m_models = models;
	m_baseprob = baseprob;

	assert(length % 4 == 0);
	int numPackages = length / 4;

	m_p_rights = new __m128[numPackages];
	m_p_totals = new __m128[numPackages];
	m_oldsizes = new __m128i[numPackages];
	for(int i = 0; i < numPackages; i++) {
		m_p_rights[i] = _mm_set1_ps(baseprob * logScale);
		m_p_totals[i] = _mm_set1_ps(baseprob*2 * logScale);
		m_oldsizes[i] = _mm_set1_epi32(BITPREC_TABLE);
	}
	m_compressedSize = BITPREC_TABLE*length;

	return true;
}

long long SoftwareCompressionStateEvaluator::changeWeight(int modelIndex, int diffw) {
	concurrency::combinable<long long> diffsize;

	int numPackages = m_models[modelIndex].numPackages;
	const int BLOCK_SIZE = 64;
	int num_blocks = (numPackages + BLOCK_SIZE - 1) / BLOCK_SIZE;

	//for(int n = 0; n < length; n++)
	concurrency::parallel_for(0, num_blocks, [&](int block)
	{
		int start_idx = block * BLOCK_SIZE;
		int end_idx = std::min(start_idx + BLOCK_SIZE, numPackages);

		
		int* packageOffsets = m_models[modelIndex].packageOffsets;
		Package* packages = m_models[modelIndex].packages;

		__m128 vlogScale = _mm_set1_ps(m_logScale);
		__m128 vdiffw = _mm_set1_ps(diffw);
		__m128i vdiffsize = _mm_setzero_si128();
		__m128* prs = m_p_rights;
		__m128* pts = m_p_totals;
		__m128i* oldsizes = m_oldsizes;

		for(int i = start_idx; i < end_idx; i++)
		{
			//IACA_VC64_START
			int packageOffset = packageOffsets[i];
			Package* package = &packages[i];

			__m128 vmodel_pr = package->p_right;
			__m128 vmodel_pt = package->p_total;

			__m128 vtotal_pr = prs[packageOffset];
			__m128 vtotal_pt = pts[packageOffset];

			vtotal_pr = _mm_add_ps(vtotal_pr, _mm_mul_ps(vmodel_pr, vdiffw));
			vtotal_pt = _mm_add_ps(vtotal_pt, _mm_mul_ps(vmodel_pt, vdiffw));
			prs[packageOffset] = vtotal_pr;
			pts[packageOffset] = vtotal_pt;

			__m128 vfr = vtotal_pr;
			__m128 vft = vtotal_pt;

			vfr = _mm_mul_ps(vfr, vfr);	vfr = _mm_mul_ps(vfr, vfr);	vfr = _mm_mul_ps(vfr, vfr);	vfr = _mm_mul_ps(vfr, vfr);
			vft = _mm_mul_ps(vft, vft);	vft = _mm_mul_ps(vft, vft);	vft = _mm_mul_ps(vft, vft);	vft = _mm_mul_ps(vft, vft);

			__m128i voldsize = oldsizes[packageOffset];
			__m128i vnewsize = _mm_srli_epi32(_mm_sub_epi32(_mm_castps_si128(vft), _mm_castps_si128(vfr)), 27 - BITPREC_TABLE_BITS);
			oldsizes[packageOffset] = vnewsize;

			vdiffsize = _mm_add_epi32(vdiffsize, _mm_sub_epi32(vnewsize, voldsize));
			//IACA_VC64_END
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
