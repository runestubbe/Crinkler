#pragma once
#ifndef _MODEL_LIST_
#define _MODEL_LIST_

#include <cstdio>

class Compressor;
enum CompressionType;

const int MAX_MODELS = 256;

struct Model {
	unsigned char weight, mask;
};

class ModelList {
	Model	m_models[MAX_MODELS];
public:
	int		nmodels;
	int		size;

	ModelList();
	ModelList(const unsigned char* models, int weightmask);
	ModelList(const ModelList& ml);
	ModelList& ModelList::operator=(const ModelList& ml);
	Model& operator[] (unsigned idx);
	const Model& operator[] (unsigned idx) const;
	void addModel(Model model);

	void setFromModelsAndMask(const unsigned char* models, int weightmask);
	void print(FILE *f) const;
	unsigned int getMaskList(unsigned char* masks, bool terminate) const;
	CompressionType detectCompressionType() const;
};

class ModelList1k
{
public:
	unsigned int modelmask;
	unsigned int boost;
	unsigned int baseprob0;
	unsigned int baseprob1;

	void print() const;
};

#endif
