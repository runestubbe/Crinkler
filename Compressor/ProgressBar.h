#ifndef _PROGRESS_BAR_H_
#define _PROGRESS_BAR_H_

class ProgressBar {
public:
	virtual ~ProgressBar() {};
	virtual void init() {};
	virtual void deinit() {};

	virtual void beginTask(const char* name) = 0;
	virtual void endTask() = 0;
	virtual void update(int n, int max) = 0;
};

#endif