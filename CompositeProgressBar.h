#ifndef _COMPOSITE_PROGRESS_BAR_H_
#define _COMPOSITE_PROGRESS_BAR_H_

#include "Compressor/ProgressBar.h"
#include <vector>

class CompositeProgressBar : public ProgressBar {
	std::vector<ProgressBar*> m_progressBars;
public:
	void init();
	void deinit();

	void beginTask(const char* name);
	void endTask();
	void update(int n, int max);

	void addProgressBar(ProgressBar* progressBar);
};

#endif