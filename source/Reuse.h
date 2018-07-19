#pragma once

#include <vector>
#include <string>

#include "Compressor/ModelList.h"
#include "Hunk.h"
#include "HunkList.h"
#include "ExplicitHunkSorter.h"

enum ReuseType {
	REUSE_OFF, REUSE_IMPROVE, REUSE_STABLE
};

static const char *reuseTypeName(ReuseType mode) {
	switch (mode) {
	case REUSE_OFF:
		return "OFF";
	case REUSE_IMPROVE:
		return "IMPROVE";
	case REUSE_STABLE:
		return "STABLE";
	}
}

class Reuse {
	ModelList *m_code_models;
	ModelList *m_data_models;

	std::vector<std::string> m_code_hunk_ids;
	std::vector<std::string> m_data_hunk_ids;
	std::vector<std::string> m_bss_hunk_ids;

	int m_hashsize;

	friend Reuse* loadReuseFile(const char *filename);
	friend void ExplicitHunkSorter::sortHunkList(HunkList* hunklist, Reuse *reuse);

public:
	Reuse();
	Reuse(const ModelList& code_models, const ModelList& data_models, const HunkList& hl, int hashsize);

	void save(const char *filename) const;

	const ModelList *getCodeModels() const { return m_code_models; }
	const ModelList *getDataModels() const { return m_data_models; }
	int getHashSize() const { return m_hashsize; }
};

Reuse* loadReuseFile(const char *filename);
