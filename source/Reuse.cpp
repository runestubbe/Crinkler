#include "Reuse.h"

#include "Log.h"
#include "MemoryFile.h"
#include "StringMisc.h"

enum State {
	INITIAL, CODE_MODELS, DATA_MODELS, CODE_HUNKS, DATA_HUNKS, BSS_HUNKS, HASHSIZE
};

#define CODE_MODELS_TAG "# Code models"
#define DATA_MODELS_TAG "# Data models"
#define CODE_HUNKS_TAG "# Code sections"
#define DATA_HUNKS_TAG "# Data sections"
#define BSS_HUNKS_TAG "# Uninitialized sections"
#define HASHSIZE_TAG "# Hash table size"

Reuse::Reuse() : m_code_models(nullptr), m_data_models(nullptr), m_hashsize(0) {}

Reuse::Reuse(const ModelList& code_models, const ModelList& data_models, const HunkList& hl, int hashsize) {
	m_code_models = new ModelList(code_models);
	m_data_models = new ModelList(data_models);
	for (int h = 0; h < hl.getNumHunks(); h++) {
		Hunk *hunk = hl[h];
		const std::string& id = hunk->getID();
		if (hunk->getRawSize() == 0) {
			m_bss_hunk_ids.push_back(id);
		}
		else if (hunk->getFlags() & HUNK_IS_CODE) {
			m_code_hunk_ids.push_back(id);
		}
		else {
			m_data_hunk_ids.push_back(id);
		}
	}
	m_hashsize = hashsize;
}

ModelList *parseModelList(const char *line) {
	ModelList *ml = new ModelList();
	int mask, weight, n;
	while (sscanf(line, " %x:%d%n", &mask, &weight, &n) == 2) {
		ml->addModel(Model{ (unsigned char)weight, (unsigned char)mask });
		line += n;
	}
	return ml;
}

Reuse* loadReuseFile(const char *filename) {
	MemoryFile mf(filename);
	if (mf.getPtr() == nullptr) return nullptr;
	Reuse *reuse = new Reuse();
	State state = INITIAL;
	for (auto line : intoLines(mf.getPtr(), mf.getSize())) {
		if (line.empty()) continue;
		if (line == CODE_MODELS_TAG) {
			state = CODE_MODELS;
		}
		else if (line == DATA_MODELS_TAG) {
			state = DATA_MODELS;
		}
		else if (line == CODE_HUNKS_TAG) {
			state = CODE_HUNKS;
		}
		else if (line == DATA_HUNKS_TAG) {
			state = DATA_HUNKS;
		}
		else if (line == BSS_HUNKS_TAG) {
			state = BSS_HUNKS;
		}
		else if (line == HASHSIZE_TAG) {
			state = HASHSIZE;
		}
		else if (line[0] == '#') {
			Log::warning(filename, "Unknown reuse file tag: %s", line.c_str());
		}
		else switch (state) {
		case CODE_MODELS:
			reuse->m_code_models = parseModelList(line.c_str());
			break;
		case DATA_MODELS:
			reuse->m_data_models = parseModelList(line.c_str());
			break;
		case CODE_HUNKS:
			reuse->m_code_hunk_ids.push_back(line);
			break;
		case DATA_HUNKS:
			reuse->m_data_hunk_ids.push_back(line);
			break;
		case BSS_HUNKS:
			reuse->m_bss_hunk_ids.push_back(line);
			break;
		case HASHSIZE:
			sscanf(line.c_str(), " %d", &reuse->m_hashsize);
			break;
		}
	}
	return reuse;
}

void Reuse::save(const char *filename) const {
	FILE* f = fopen(filename, "w");
	fprintf(f, "\n%s\n", CODE_MODELS_TAG);
	m_code_models->print(f);
	fprintf(f, "\n%s\n", DATA_MODELS_TAG);
	m_data_models->print(f);
	fprintf(f, "\n%s\n", CODE_HUNKS_TAG);
	for (auto id : m_code_hunk_ids) {
		fprintf(f, "%s\n", id.c_str());
	}
	fprintf(f, "\n%s\n", DATA_HUNKS_TAG);
	for (auto id : m_data_hunk_ids) {
		fprintf(f, "%s\n", id.c_str());
	}
	fprintf(f, "\n%s\n", BSS_HUNKS_TAG);
	for (auto id : m_bss_hunk_ids) {
		fprintf(f, "%s\n", id.c_str());
	}
	fprintf(f, "\n%s\n", HASHSIZE_TAG);
	fprintf(f, "%d\n", m_hashsize);
	fclose(f);
}
