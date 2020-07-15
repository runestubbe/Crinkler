#pragma once
#ifndef _CONSOLE_PROGRESS_BAR_H_
#define _CONSOLE_PROGRESS_BAR_H_

#include "ProgressBar.h"

class ConsoleProgressBar : public ProgressBar {
	int m_pos;
	int m_stime;
public:
	void BeginTask(const char* name);
	void EndTask();
	void Update(int n, int max);
};

#endif