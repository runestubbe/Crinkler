#pragma once

#include <vector>
#include <string>

#include "Compressor/ModelList.h"
#include "Hunk.h"
#include "HunkList.h"
#include "ExplicitHunkSorter.h"

enum ReuseType {
	REUSE_OFF, REUSE_WRITE, REUSE_IMPROVE, REUSE_STABLE
};

static const char *ReuseTypeName(ReuseType mode) {
	switch (mode) {
	case REUSE_OFF:
		return "OFF";
	case REUSE_WRITE:
		return "WRITE";
	case REUSE_IMPROVE:
		return "IMPROVE";
	case REUSE_STABLE:
		return "STABLE";
	}
	return "";
}

class Reuse {
	ModelList *m_code_models;
	ModelList *m_data_models;

	std::vector<std::string> m_code_hunk_ids;
	std::vector<std::string> m_data_hunk_ids;
	std::vector<std::string> m_bss_hunk_ids;

	int m_hashsize;

	friend Reuse* LoadReuseFile(const char *filename);
	friend void ExplicitHunkSorter::SortHunkList(HunkList* hunklist, Reuse *reuse);

public:
	Reuse();
	Reuse(const ModelList& code_models, const ModelList& data_models, const HunkList& hl, int hashsize);

	const ModelList*	GetCodeModels() const { return m_code_models; }
	const ModelList*	GetDataModels() const { return m_data_models; }
	int					GetHashSize() const { return m_hashsize; }

	void				Save(const char* filename) const;
};

Reuse* LoadReuseFile(const char *filename);
