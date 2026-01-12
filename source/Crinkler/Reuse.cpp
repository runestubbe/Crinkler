#include "Reuse.h"

#include "Log.h"
#include "MemoryFile.h"
#include "StringMisc.h"

#include <algorithm>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
	: m_name(part.GetName()), m_models(part.m_initialized ? &part.m_model4k : nullptr), m_initialized(part.IsInitialized()) {
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
		case INITIAL:
			Log::Warning(filename, "Unexpected line: %s", line.c_str());
			break;
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
		} else if (part.m_initialized) {
			Log::Error(filename, "Models missing for part '%s'", part.m_name.c_str());
		}
	}
	for (auto models : models_by_name) {
		if (models.second != nullptr) {
			Log::Warning(filename, "Models specified for unknown part '%s'", models.first.c_str());
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

static std::vector<Hunk*> PickHunks(const std::vector<std::string>& ids, std::unordered_map<std::string, Hunk*>& hunk_by_id, bool* mismatch) {
	std::vector<Hunk*> hunks;
	for (const std::string& id : ids) {
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			hunks.push_back(hunk_it->second);
			hunk_by_id.erase(hunk_it);
		} else {
			Log::Warning("", "Reused section not present: %s", id.c_str());
			*mismatch = true;
		}
	}
	return hunks;
}

bool Reuse::PartsFromHunkList(PartList& parts, Part& hunkList) {
	bool mismatch = false;

	std::unordered_map<std::string, Hunk*> hunk_by_id;
	hunkList.ForEachHunk([&hunk_by_id](Hunk* hunk) { hunk_by_id[hunk->GetID()] = hunk; });

	std::vector<std::vector<Hunk*>> part_hunks;
	for (ReusePart& reuse_part : m_parts) {
		part_hunks.push_back(PickHunks(reuse_part.m_hunk_ids, hunk_by_id, &mismatch));
		parts.GetOrAddPart(reuse_part.m_name.c_str(), reuse_part.m_initialized);
	}
	
	hunkList.ForEachHunk([&parts, &part_hunks, &hunk_by_id, &mismatch](Hunk* hunk) {
		const std::string& id = hunk->GetID();
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			int part_index = parts.FindBestPartIndex(hunk);
			part_hunks[part_index].push_back(hunk);

			Log::Warning("", "Section not present in reuse file: %s (added to %s)", id.c_str(), parts[part_index].GetName());
			hunk_by_id.erase(hunk_it);
			mismatch = true;
		}
	});
	
	// Copy hunks back to parts
	assert(parts.IsEmpty());
	for (size_t i = 0; i < m_parts.size(); i++) {
		ReusePart& reuse_part = m_parts[i];
		Part& part = parts.GetOrAddPart(reuse_part.m_name.c_str(), reuse_part.m_initialized);
		for (Hunk* hunk : part_hunks[i]) {
			part.AddHunkBack(hunk);
		}
		if (part.m_initialized) {
			part.m_model4k = *reuse_part.m_models;
		}
	}

	return mismatch;
}


static INT_PTR CALLBACK ReuseDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_INITDIALOG:
		return TRUE;
	case WM_COMMAND:
		{
			int id = LOWORD(wParam);
			if (id == IDCANCEL) {
				EndDialog(hwnd, REUSE_DEFAULT);
				return TRUE;
			}
			if (id >= 100) {
				EndDialog(hwnd, id - 100);
				return TRUE;
			}
		}
		break;
	case WM_CLOSE:
		EndDialog(hwnd, REUSE_DEFAULT);
		return TRUE;
	}
	return FALSE;
}

// Helper to align data on DWORD boundary
static LPWORD lpwAlign(LPWORD lpIn) {
	ULONG_PTR ul = (ULONG_PTR)lpIn;
	ul += 3;
	ul >>= 2;
	ul <<= 2;
	return (LPWORD)ul;
}

#pragma pack(push, 1)
typedef struct {
	DWORD style;
	DWORD dwExtendedStyle;
	WORD cdit;
	short x;
	short y;
	short cx;
	short cy;
} MyDLGTEMPLATE;

typedef struct {
	DWORD style;
	DWORD dwExtendedStyle;
	short x;
	short y;
	short cx;
	short cy;
	WORD id;
} MyDLGITEMTEMPLATE;
#pragma pack(pop)

ReuseType AskForReuseMode() {
	HGLOBAL hgbl;
	MyDLGTEMPLATE* lpdt;
	MyDLGITEMTEMPLATE* lpdit;
	LPWORD lpw;
	LPWSTR lpwsz;
	int nchar;

	hgbl = GlobalAlloc(GMEM_ZEROINIT, 4096);
	if (!hgbl) return REUSE_DEFAULT;

	lpdt = (MyDLGTEMPLATE*)GlobalLock(hgbl);

	// Define a dialog box.
	lpdt->style = WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | WS_CAPTION | DS_CENTER | WS_VISIBLE;
	lpdt->cdit = (REUSE_ALL - REUSE_OFF + 1) * 2;         // Number of controls
	lpdt->x = 0;  lpdt->y = 0;
	lpdt->cx = 190; lpdt->cy = 110;

	lpw = (LPWORD)(lpdt + 1);
	*lpw++ = 0;             // No menu
	*lpw++ = 0;             // Predefined dialog box class (by default)

	lpwsz = (LPWSTR)lpw;
	nchar = MultiByteToWideChar(CP_ACP, 0, "Select Reuse Mode", -1, lpwsz, 50);
	lpw += nchar;

	for (int i = REUSE_OFF; i <= REUSE_ALL; i++) {
		ReuseType type = (ReuseType)i;
		const char* text = ReuseTypeName(type);
		const char* desc = ReuseTypeDescription(type);
		int y = 8 + i * 16;

		// Button
		lpw = lpwAlign(lpw);
		lpdit = (MyDLGITEMTEMPLATE*)lpw;
		lpdit->x = 10; lpdit->y = (short)y;
		lpdit->cx = 50; lpdit->cy = 13;
		lpdit->id = (WORD)(i + 100);
		lpdit->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

		lpw = (LPWORD)(lpdit + 1);
		*lpw++ = 0xFFFF;
		*lpw++ = 0x0080;        // Button class

		std::string buttonText = "&";
		buttonText += text;
		lpwsz = (LPWSTR)lpw;
		nchar = MultiByteToWideChar(CP_ACP, 0, buttonText.c_str(), -1, lpwsz, 50);
		lpw += nchar;
		*lpw++ = 0;             // No creation data

		// Description
		lpw = lpwAlign(lpw);
		lpdit = (MyDLGITEMTEMPLATE*)lpw;
		lpdit->x = 68; lpdit->y = (short)(y + 2);
		lpdit->cx = 150; lpdit->cy = 14;
		lpdit->id = -1;
		lpdit->style = WS_CHILD | WS_VISIBLE | SS_LEFT;

		lpw = (LPWORD)(lpdit + 1);
		*lpw++ = 0xFFFF;
		*lpw++ = 0x0082;        // Static class

		lpwsz = (LPWSTR)lpw;
		nchar = MultiByteToWideChar(CP_ACP, 0, desc, -1, lpwsz, 100);
		lpw += nchar;
		*lpw++ = 0;             // No creation data
	}

	GlobalUnlock(hgbl);
	INT_PTR ret = DialogBoxIndirect(GetModuleHandle(NULL),
		(LPDLGTEMPLATE)hgbl,
		NULL,
		(DLGPROC)ReuseDialogProc);
	GlobalFree(hgbl);

	return (ReuseType)ret;
}
