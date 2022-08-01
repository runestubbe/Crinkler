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
		}
		else {
			Log::Warning("", "Reused hunk not present: %s", id.c_str());
		}
	}
	return hunks;
}

void ExplicitHunkSorter::SortHunks(PartList& parts, Reuse *reuse) {
	std::unordered_map<std::string, Hunk*> hunk_by_id;
	parts.ForEachHunk([&hunk_by_id](Hunk* hunk) { hunk_by_id[hunk->GetID()] = hunk; });

	/*
	// REFACTOR_TODO
	std::vector<Hunk*> code_hunks = PickHunks(reuse->m_code_hunk_ids, hunk_by_id);
	std::vector<Hunk*> data_hunks = PickHunks(reuse->m_data_hunk_ids, hunk_by_id);
	std::vector<Hunk*> bss_hunks = PickHunks(reuse->m_bss_hunk_ids, hunk_by_id);

	for (int h = 0; h < hunklist->GetNumHunks(); h++) {
		Hunk *hunk = (*hunklist)[h];
		const std::string& id = hunk->GetID();
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			Log::Warning("", "Hunk not present in reuse file: %s", id.c_str());
			if (hunk->GetRawSize() == 0) {
				bss_hunks.push_back(hunk);
			}
			else if (hunk->GetFlags() & HUNK_IS_CODE) {
				code_hunks.push_back(hunk);
			}
			else {
				data_hunks.push_back(hunk);
			}
		}
	}
	assert(hunk_by_id.empty());

	// Copy hunks back to hunklist
	hunklist->Clear();
	for (Hunk *hunk : code_hunks) hunklist->AddHunkBack(hunk);
	for (Hunk *hunk : data_hunks) hunklist->AddHunkBack(hunk);
	for (Hunk *hunk : bss_hunks) hunklist->AddHunkBack(hunk);
	*/
}
