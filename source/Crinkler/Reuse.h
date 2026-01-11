#pragma once

#include <vector>
#include <string>

#include "../Compressor/ModelList.h"
#include "Hunk.h"
#include "PartList.h"

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

	void				PartsFromHunkList(PartList& parts, Part& hunkList);

	int					GetHashSize() const { return m_hashsize; }

	void				Save(const char* filename) const;

	
};

Reuse* LoadReuseFile(const char *filename);
