#pragma once
#ifndef _PROGRESS_BAR_H_
#define _PROGRESS_BAR_H_

class ProgressBar {
public:
	virtual ~ProgressBar() {};
	
	virtual void Init() {};
	virtual void Deinit() {};

	virtual void BeginTask(const char* name) = 0;
	virtual void EndTask() = 0;
	virtual void Update(int n, int max) = 0;
};

#endif
