#pragma once
#ifndef _WINDOW_PROGRESS_BAR_H_
#define _WINDOW_PROGRESS_BAR_H_

#include <windows.h>
#include <string>
#include "Compressor/ProgressBar.h"

class WindowProgressBar : public ProgressBar{
	HANDLE m_thread;
	int m_maxValue;
	int m_stime;
	std::string m_name;
public:
	void init();
	void beginTask(const char* name);
	void endTask();
	void update(int n, int max);
};

#endif
