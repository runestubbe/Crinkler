#pragma once
#ifndef _COMPOSITE_PROGRESS_BAR_H_
#define _COMPOSITE_PROGRESS_BAR_H_

#include "ProgressBar.h"
#include <vector>

class CompositeProgressBar : public ProgressBar {
	std::vector<ProgressBar*> m_progressBars;
public:
	void Init();
	void Deinit();

	void BeginTask(const char* name);
	void EndTask();
	void Update(int n, int max);

	void AddProgressBar(ProgressBar* progressBar);
	void ClearProgressBars() { m_progressBars.clear(); };
};

#endif