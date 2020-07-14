#include "ExplicitHunkSorter.h"

#include "HunkList.h"
#include "Hunk.h"
#include "Symbol.h"
#include "Reuse.h"
#include "Log.h"

#include <cassert>
#include <unordered_map>
#include <vector>

std::vector<Hunk*> pickHunks(const std::vector<std::string>& ids, std::unordered_map<std::string, Hunk*>& hunk_by_id) {
	std::vector<Hunk*> hunks;
	for (const std::string& id : ids) {
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			hunks.push_back(hunk_it->second);
			hunk_by_id.erase(hunk_it);
		}
		else {
			Log::warning("", "Reused hunk not present: %s", id.c_str());
		}
	}
	return hunks;
}

void ExplicitHunkSorter::sortHunkList(HunkList* hunklist, Reuse *reuse) {
	std::unordered_map<std::string, Hunk*> hunk_by_id;
	for (int h = 0; h < hunklist->getNumHunks(); h++) {
		Hunk *hunk = (*hunklist)[h];
		hunk_by_id[hunk->getID()] = hunk;
	}

	std::vector<Hunk*> code_hunks = pickHunks(reuse->m_code_hunk_ids, hunk_by_id);
	std::vector<Hunk*> data_hunks = pickHunks(reuse->m_data_hunk_ids, hunk_by_id);
	std::vector<Hunk*> bss_hunks = pickHunks(reuse->m_bss_hunk_ids, hunk_by_id);

	for (int h = 0; h < hunklist->getNumHunks(); h++) {
		Hunk *hunk = (*hunklist)[h];
		const std::string& id = hunk->getID();
		auto hunk_it = hunk_by_id.find(id);
		if (hunk_it != hunk_by_id.end()) {
			Log::warning("", "Hunk not present in reuse file: %s", id.c_str());
			if (hunk->getRawSize() == 0) {
				bss_hunks.push_back(hunk);
			}
			else if (hunk->getFlags() & HUNK_IS_CODE) {
				code_hunks.push_back(hunk);
			}
			else {
				data_hunks.push_back(hunk);
			}
		}
	}
	assert(hunk_by_id.empty());

	// Copy back hunks to hunklist
	hunklist->clear();
	for (Hunk *hunk : code_hunks) hunklist->addHunkBack(hunk);
	for (Hunk *hunk : data_hunks) hunklist->addHunkBack(hunk);
	for (Hunk *hunk : bss_hunks) hunklist->addHunkBack(hunk);
}
