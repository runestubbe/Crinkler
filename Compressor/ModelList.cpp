#include <memory>
#include <cassert>
#include "ModelList.h"

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
	if(nmodels > 0)
		memcpy(m_models, ml.m_models, nmodels*sizeof(Model));
}

ModelList::ModelList(const unsigned char* models, int weightmask) {
	setFromModelsAndMask(models, weightmask);
}

ModelList& ModelList::operator=(const ModelList& ml) {
	this->nmodels = ml.nmodels;
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

void ModelList::print() const {
	printf("Models:");
	for(int m = 0 ; m < nmodels; m++) {
		printf(" %02X:%d", m_models[m].mask, m_models[m].weight);
	}
	printf("\n");
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

	return weightmask & (-2 + (terminate ^ parity(weightmask)));
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
			Model m = {weight, models[nmodels]};
			m_models[nmodels] = m;
			nmodels++;
		}
	} while(weightmask);
}
