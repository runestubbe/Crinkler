#ifndef _CONSOLE_PROGRESS_BAR_H_
#define _CONSOLE_PROGRESS_BAR_H_

#include "Compressor/ProgressBar.h"

class ConsoleProgressBar : public ProgressBar {
	int m_pos;
	int m_stime;
public:
	void beginTask(const char* name);
	void endTask();
	void update(int n, int max);
};

#endif