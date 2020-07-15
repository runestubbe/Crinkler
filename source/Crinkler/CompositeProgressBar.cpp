#include "CompositeProgressBar.h"

using namespace std;

void CompositeProgressBar::Init() {
	for(ProgressBar* progressBar : m_progressBars)
		progressBar->Init();
}

void CompositeProgressBar::Deinit() {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->Deinit();
}

void CompositeProgressBar::BeginTask(const char* name) {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->BeginTask(name);
}

void CompositeProgressBar::EndTask() {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->EndTask();
}

void CompositeProgressBar::Update(int n, int max) {
	for (ProgressBar* progressBar : m_progressBars)
		progressBar->Update(n, max);
}

void CompositeProgressBar::AddProgressBar(ProgressBar* progressBar) {
	m_progressBars.push_back(progressBar);
}
