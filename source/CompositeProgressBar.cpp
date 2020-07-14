#include "CompositeProgressBar.h"

using namespace std;

void CompositeProgressBar::init() {
	for(ProgressBar* progressBar : m_progressBars)
		progressBar->init();
}

void CompositeProgressBar::deinit() {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->deinit();
}

void CompositeProgressBar::beginTask(const char* name) {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->beginTask(name);
}

void CompositeProgressBar::endTask() {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->endTask();
}

void CompositeProgressBar::update(int n, int max) {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->update(n, max);
}

void CompositeProgressBar::addProgressBar(ProgressBar* progressBar) {
	m_progressBars.push_back(progressBar);
}
