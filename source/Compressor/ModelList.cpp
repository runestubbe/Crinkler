#include <memory>
#include <cassert>
#include "ModelList.h"
#include "Compressor.h"

using namespace std;

static int parity(int n) {
	int p = 0;
	for (int i = 0 ; i < 8 ; i++) {
		p ^= (n >> i) & 1;
	}

	return p;
}


ModelList::ModelList() :
	nmodels(0)
{

}

ModelList::ModelList(const ModelList& ml) {
	this->nmodels = ml.nmodels;
	this->size = ml.size;
	if(nmodels > 0)
		memcpy(m_models, ml.m_models, nmodels*sizeof(Model));
}

ModelList::ModelList(const unsigned char* models, int weightmask) {
	setFromModelsAndMask(models, weightmask);
}

ModelList& ModelList::operator=(const ModelList& ml) {
	this->nmodels = ml.nmodels;
	this->size = ml.size;
	if(nmodels > 0)
		memcpy(m_models, ml.m_models, nmodels*sizeof(Model));
	return *this;
}

Model& ModelList::operator[] (unsigned idx) {
	assert(idx < MAX_MODELS);
	return m_models[idx];
}

const Model& ModelList::operator[] (unsigned idx) const {
	assert(idx < MAX_MODELS);
	return m_models[idx];
}

void ModelList::addModel(Model model) {
	m_models[nmodels++] = model;
}

void ModelList::print(FILE *f) const {
	for(int m = 0 ; m < nmodels; m++) {
		fprintf(f, "%s%02X:%d", m == 0 ? "" : " ", m_models[m].mask, m_models[m].weight);
	}
	fprintf(f, "\n");
}

//copy a sorted modellist to masks and return the corresponding weightmask
unsigned int ModelList::getMaskList(unsigned char* masks, bool terminate) const {
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

	//pad with ones
	while (biti >= 0) {
		weightmask |= 1 << biti;
		biti--;
	}

	return weightmask & (-2 + (int(terminate) ^ parity(weightmask)));
}

void ModelList::setFromModelsAndMask(const unsigned char* models, int weightmask) {
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

CompressionType ModelList::detectCompressionType() const {
	// This ocde does not work, as FAST mode has a single
	// weight optimization at the end.
	ModelList instant = InstantModels();
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


void ModelList1k::print() const
{
	printf("Models: %08X   Boost: %d  BaseProb: (%d, %d)\n", modelmask, boost, baseprob0, baseprob1);
}