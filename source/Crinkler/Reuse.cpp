#include "Reuse.h"

#include "Log.h"
#include "MemoryFile.h"
#include "StringMisc.h"

#include <algorithm>

enum State {
	INITIAL, MODELS, SECTIONS, HASHSIZE
};

#define MODELS_TAG "models"
#define SECTIONS_TAG "sections"
#define UNINITIALIZED_NAME "Uninitialized"
#define HASHSIZE_TAG "# Hash table size"

static ModelList4k *ParseModelList(const char *line) {
	ModelList4k *ml = new ModelList4k();
	int mask, weight, n;
	while (sscanf(line, " %x:%d%n", &mask, &weight, &n) == 2) {
		ml->AddModel(Model{ (unsigned char)weight, (unsigned char)mask });
		line += n;
	}
	return ml;
}

ReusePart::ReusePart(Part& part)
	: m_name(part.GetName()), m_models(&part.m_model4k), m_initialized(part.IsInitialized()) {
	part.ForEachHunk([this](Hunk* hunk) {
		m_hunk_ids.push_back(hunk->GetID());
	});
}

ReusePart::ReusePart(std::string name, bool initialized)
	: m_name(std::move(name)), m_models(nullptr), m_initialized(initialized) {}

Reuse::Reuse() : m_hashsize(0) {}

Reuse::Reuse(PartList& parts, int hashsize) : m_hashsize(hashsize) {
	parts.ForEachPart([this](Part& part, int p) {
		m_parts.emplace_back(part);
	});
}

Reuse* LoadReuseFile(const char *filename) {
	MemoryFile mf(filename, false);
	if (mf.GetPtr() == nullptr) return nullptr;
	Reuse *reuse = new Reuse();
	std::map<std::string, ModelList4k*> models_by_name;
	State state = INITIAL;
	ModelList4k** current_models = nullptr;
	for (auto line : IntoLines(mf.GetPtr(), mf.GetSize())) {
		if (line.empty()) continue;
		if (line == HASHSIZE_TAG) {
			if (reuse->m_hashsize != 0) {
				Log::Error(filename, "Duplicate hash size: %s", line.c_str());
			}
			state = HASHSIZE;
		} else if (line[0] == '#') {
			// Skip leading spaces
			size_t start = 1;
			while (start < line.length() && line[start] == ' ') start++;
			size_t end = line.rfind(' ');
			if (end != std::string::npos && end > start) {
				std::string name = line.substr(start, end - start);
				std::string tag = line.substr(end + 1);
				bool initialized = name != UNINITIALIZED_NAME;
				if (tag == MODELS_TAG) {
					if (models_by_name.count(name)) {
						Log::Error(filename, "Duplicate part: %s", line.c_str());
					}
					state = MODELS;
					current_models = &models_by_name[name];
				} else if (tag == SECTIONS_TAG) {
					const auto this_name = [&name](ReusePart& part) { return part.m_name == name; };
					if (std::any_of(reuse->m_parts.begin(), reuse->m_parts.end(), this_name)) {
						Log::Error(filename, "Duplicate part: %s", line.c_str());
					}
					state = SECTIONS;
					reuse->m_parts.emplace_back(name, initialized);
				} else {
					Log::Warning(filename, "Unknown reuse file tag: %s", line.c_str());
				}
			} else {
				Log::Warning(filename, "Unknown reuse file tag: %s", line.c_str());
			}
		} else switch (state) {
		case MODELS:
			if (*current_models != nullptr) {
				Log::Error(filename, "Duplicate models: %s", line.c_str());
			}
			*current_models = ParseModelList(line.c_str());
			break;
		case SECTIONS:
			reuse->m_parts.back().m_hunk_ids.push_back(line);
			break;
		case HASHSIZE:
			if (reuse->m_hashsize != 0) {
				Log::Error(filename, "Duplicate hash size: %s", line.c_str());
			}
			sscanf(line.c_str(), " %d", &reuse->m_hashsize);
			break;
		}
	}

	if (reuse->m_parts.back().m_initialized) {
		Log::Error(filename, "The 'Uninitialized' part must be last");
	}

	// Attach models to parts
	for (ReusePart& part : reuse->m_parts) {
		if (models_by_name.count(part.m_name)) {
			part.m_models = models_by_name[part.m_name];
			models_by_name.erase(part.m_name);
		}
	}
	for (auto models : models_by_name) {
		if (models.second != nullptr) {
			Log::Warning(filename, "Models specified for unknown part '%s'", models.first);
			delete models.second;
		}
	}

	return reuse;
}

void Reuse::Save(const char *filename) const {
	FILE* f = fopen(filename, "w");
	for (const ReusePart& part : m_parts) {
		if (part.m_models != nullptr) {
			fprintf(f, "\n# %s %s\n", part.m_name.c_str(), MODELS_TAG);
			part.m_models->Print(f);
		}
	}
	for (const ReusePart& part : m_parts) {
		fprintf(f, "\n# %s %s\n", part.m_name.c_str(), SECTIONS_TAG);
		for (auto id : part.m_hunk_ids) {
			fprintf(f, "%s\n", id.c_str());
		}
	}
	if (m_hashsize != 0) {
		fprintf(f, "\n%s\n", HASHSIZE_TAG);
		fprintf(f, "%d\n", m_hashsize);
	}
	fclose(f);
}
