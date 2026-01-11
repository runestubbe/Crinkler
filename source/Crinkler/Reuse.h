#pragma once

#include <vector>
#include <string>

#include "../Compressor/ModelList.h"
#include "Hunk.h"
#include "PartList.h"

enum ReuseType {
	REUSE_OFF, REUSE_WRITE, REUSE_PARTS, REUSE_SECTIONS, REUSE_MODELS, REUSE_ALL
};

static const char *ReuseTypeName(ReuseType mode) {
	switch (mode) {
	case REUSE_OFF:
		return "OFF";
	case REUSE_WRITE:
		return "WRITE";
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
