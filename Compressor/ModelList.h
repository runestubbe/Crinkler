#pragma once
#ifndef _MODEL_LIST_
#define _MODEL_LIST_

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

	ModelList();
	ModelList(const unsigned char* models, int weightmask);
	ModelList(const ModelList& ml);
	ModelList& ModelList::operator=(const ModelList& ml);
	Model& operator[] (unsigned idx);
	const Model& operator[] (unsigned idx) const;

	void setFromModelsAndMask(const unsigned char* models, int weightmask);
	void print() const;
	unsigned int getMaskList(unsigned char* masks, bool terminate) const;
	CompressionType detectCompressionType() const;
};

#endif
