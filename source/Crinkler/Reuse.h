#pragma once

#include <vector>
#include <string>

#include "../Compressor/ModelList.h"
#include "Hunk.h"
#include "PartList.h"

enum ReuseType {
	REUSE_OFF, REUSE_NOTHING, REUSE_PARTS, REUSE_SECTIONS, REUSE_MODELS, REUSE_ALL
};

static const char *ReuseTypeName(ReuseType mode) {
	switch (mode) {
	case REUSE_OFF:
		return "OFF";
	case REUSE_NOTHING:
		return "NOTHING";
	case REUSE_PARTS:
		return "PARTS";
	case REUSE_SECTIONS:
		return "SECTIONS";
	case REUSE_MODELS:
		return "MODELS";
	case REUSE_ALL:
		return "ALL";
	}
	return "";
}

static const char *ReuseTypeDescription(ReuseType mode) {
	switch (mode) {
	case REUSE_OFF:
		return "Ignore reuse file";
	case REUSE_NOTHING:
		return "Ignore and overwrite reuse file";
	case REUSE_PARTS:
		return "Reuse parts";
	case REUSE_SECTIONS:
		return "Reuse parts and sections";
	case REUSE_MODELS:
		return "Reuse parts, sections and models";
	case REUSE_ALL:
		return "Reuse everything";
	}
	return "";
}

class ReusePart {
public:
	ReusePart(Part& part);
	ReusePart(std::string name, bool initialized);

	std::string m_name;
	std::vector<std::string> m_hunk_ids;
	const ModelList4k *m_models;
	bool m_initialized;
};

class Reuse {
	std::vector<ReusePart> m_parts;

	int m_hashsize;

	friend Reuse* LoadReuseFile(const char *filename);

public:
	Reuse();
	Reuse(PartList& parts, int hashsize);

	bool				PartsFromHunkList(PartList& parts, Part& hunkList);

	int					GetHashSize() const { return m_hashsize; }

	void				Save(const char* filename) const;

	
};

Reuse* LoadReuseFile(const char *filename);

ReuseType AskForReuseMode(ReuseType active);
