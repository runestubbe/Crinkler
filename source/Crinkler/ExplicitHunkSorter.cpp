#include "ExplicitHunkSorter.h"

#include "PartList.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Reuse.h"
#include "Log.h"

#include <cassert>
#include <unordered_map>
#include <vector>

static std::vector<Hunk*> PickHunks(const std::vector<std::string>& ids, std::unordered_map<std::string, Hunk*>& hunk_by_id) {
	std::vector<Hunk*> hunks;
	for (const std::string& id : ids) {
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			hunks.push_back(hunk_it->second);
			hunk_by_id.erase(hunk_it);
		} else {
			Log::Warning("", "Reused section not present: %s", id.c_str());
		}
	}
	return hunks;
}

void ExplicitHunkSorter::SortHunks(PartList& parts, Reuse *reuse) {
	std::unordered_map<std::string, Hunk*> hunk_by_id;
	parts.ForEachHunk([&hunk_by_id](Hunk* hunk) { hunk_by_id[hunk->GetID()] = hunk; });

	std::vector<std::vector<Hunk*>> part_hunks;
	for (ReusePart& reuse_part : reuse->m_parts) {
		part_hunks.push_back(PickHunks(reuse_part.m_hunk_ids, hunk_by_id));
	}

	parts.ForEachHunk([&parts, &part_hunks, &hunk_by_id](Hunk* hunk) {
		const std::string& id = hunk->GetID();
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			const char* part_name;
			if (hunk->GetRawSize() == 0) {
				part_hunks.back().push_back(hunk);
				part_name = parts.GetUninitializedPart().GetName();
			} else if (hunk->GetFlags() & HUNK_IS_CODE) {
				part_hunks[PartList::CODE_PART_INDEX].push_back(hunk);
				part_name = parts.GetCodePart().GetName();
			} else {
				part_hunks[PartList::DATA_PART_INDEX].push_back(hunk);
				part_name = parts.GetDataPart().GetName();
			}
			Log::Warning("", "Section not present in reuse file: %s (added to %s)", id.c_str(), part_name);
			hunk_by_id.erase(hunk_it);
		}
	});
	assert(hunk_by_id.empty());

	// Copy hunks back to hunklists
	parts.Clear();
	for (size_t i = 0; i < reuse->m_parts.size(); i++) {
		ReusePart& reuse_part = reuse->m_parts[i];
		Part& part = parts.GetOrAddPart(reuse_part.m_name.c_str(), reuse_part.m_initialized);
		for (Hunk* hunk : part_hunks[i]) {
			part.AddHunkBack(hunk);
		}
		if (part.m_initialized) {
			part.m_model4k = *reuse_part.m_models;
		}
	}
}
