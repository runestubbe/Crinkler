#include <memory>
#include <cassert>
#include "ModelList.h"
#include "Compressor.h"

static int Parity(int n) {
	int p = 0;
	for (int i = 0 ; i < 8 ; i++) {
		p ^= (n >> i) & 1;
	}

	return p;
}


ModelList4k::ModelList4k() :
	nmodels(0)
{

}

ModelList4k::ModelList4k(const ModelList4k& ml) {
	this->nmodels = ml.nmodels;
	this->size = ml.size;
	if(nmodels > 0)
		memcpy(m_models, ml.m_models, nmodels*sizeof(Model));
}

ModelList4k::ModelList4k(const unsigned char* models, int weightmask) {
	SetFromModelsAndMask(models, weightmask);
}

ModelList4k& ModelList4k::operator=(const ModelList4k& ml) {
	this->nmodels = ml.nmodels;
	this->size = ml.size;
	if(nmodels > 0)
		memcpy(m_models, ml.m_models, nmodels*sizeof(Model));
	return *this;
}

Model& ModelList4k::operator[] (unsigned idx) {
	assert(idx < MAX_MODELS);
	return m_models[idx];
}

const Model& ModelList4k::operator[] (unsigned idx) const {
	assert(idx < MAX_MODELS);
	return m_models[idx];
}

void ModelList4k::AddModel(Model model) {
	m_models[nmodels++] = model;
}

void ModelList4k::Print(FILE *f) const {
	for(int m = 0 ; m < nmodels; m++) {
		fprintf(f, "%s%02X:%d", m == 0 ? "" : " ", m_models[m].mask, m_models[m].weight);
	}
	fprintf(f, "\n");
	for (int m = 0; m < nmodels; m++) {
		for (int i = 0; i < 32; i++) {
			if (i != 0 && (i & 7) == 0) fprintf(f, " ");
			fprintf(f, "%d", (m_models[m].mask >> i) & 1);
		}
			
		printf(": %d\n", m_models[m].weight);
	}
	fprintf(f, "\n");
}

// Copy a sorted model list to masks and return the corresponding weight mask
unsigned int ModelList4k::GetMaskList(unsigned char* masks, bool terminate) const {
	unsigned int weightmask = 0;
	int nmodels = this->nmodels;
	int biti = 31;
	for(int w = 0; nmodels; w++) {
		for(int m = 0; m < this->nmodels; m++) {
			if(m_models[m].weight == w) {
				if(masks)
					*masks++ = (unsigned char)m_models[m].mask;
				nmodels--;
				biti--;
			}
		}
		weightmask |= (1 << biti);
		biti--;
	}

	// Pad with ones
	while (biti >= 0) {
		weightmask |= 1 << biti;
		biti--;
	}

	return weightmask & (-2 + (int(terminate) ^ Parity(weightmask)));
}

void ModelList4k::SetFromModelsAndMask(const unsigned char* models, int weightmask) {
	nmodels = 0;
	int weight = 0;
	do {
		while(weightmask & 0x80000000) {
			weight++;
			weightmask <<= 1;
		}
		weightmask <<= 1;

		if(weightmask) {
			Model m = {(unsigned char)weight, models[nmodels]};
			m_models[nmodels] = m;
			nmodels++;
		}
	} while(weightmask);
}

CompressionType ModelList4k::DetectCompressionType() const {
	// This code does not work, as FAST mode has a single
	// weight optimization at the end.
	ModelList4k instant = InstantModels4k();
	bool is_instant = true;
	bool is_fast = true;
	for (int i = 0 ; i < nmodels ; i++)
	{
		bool found_instant = false;
		for (int j = 0 ; j < instant.nmodels ; j++)
		{
			if (m_models[i].mask == instant.m_models[j].mask &&
				m_models[i].weight == instant.m_models[j].weight)
			{
				found_instant = true;
			}
		}
		if (!found_instant) is_instant = false;
		int n_bits = m_models[i].mask;
		n_bits = (n_bits & 0x55) + ((n_bits & 0xaa) >> 1);
		n_bits = (n_bits & 0x33) + ((n_bits & 0xcc) >> 2);
		n_bits = (n_bits & 0x0f) + ((n_bits & 0xf0) >> 4);
		if (n_bits != m_models[i].weight) is_fast = false;
	}
	if (is_instant) return COMPRESSION_INSTANT;
	if (is_fast) return COMPRESSION_FAST;
	return COMPRESSION_SLOW;
}

void ModelList1k::Print() const
{
	printf("Models: %08X   Boost: %d  BaseProb: (%d, %d)\n", modelmask, boost, baseprob0, baseprob1);
}