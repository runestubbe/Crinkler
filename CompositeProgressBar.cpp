#include "CompositeProgressBar.h"

using namespace std;

void CompositeProgressBar::init() {
	for(vector<ProgressBar*>::iterator it = m_progressBars.begin(); it != m_progressBars.end(); it++)
		(*it)->init();
}

void CompositeProgressBar::deinit() {
	for(vector<ProgressBar*>::iterator it = m_progressBars.begin(); it != m_progressBars.end(); it++)
		(*it)->deinit();
}

void CompositeProgressBar::beginTask(const char* name) {
	for(vector<ProgressBar*>::iterator it = m_progressBars.begin(); it != m_progressBars.end(); it++)
		(*it)->beginTask(name);
}

void CompositeProgressBar::endTask() {
	for(vector<ProgressBar*>::iterator it = m_progressBars.begin(); it != m_progressBars.end(); it++)
		(*it)->endTask();
}

void CompositeProgressBar::update(int n, int max) {
	for(vector<ProgressBar*>::iterator it = m_progressBars.begin(); it != m_progressBars.end(); it++)
		(*it)->update(n, max);
}

void CompositeProgressBar::addProgressBar(ProgressBar* progressBar) {
	m_progressBars.push_back(progressBar);
}
