#include "ConsoleProgressBar.h"

#include <cstring>
#include <ctime>
#include <cstdio>

using namespace std;

const int PROGRESS_BAR_WIDTH = 60;

void ConsoleProgressBar::beginTask(const char* name) {
	printf("\n|-- %s ", name);
	for(int i = 0; i < (int)(PROGRESS_BAR_WIDTH-6-strlen(name)); i++)
		printf("-");
	printf("|\n");
	fflush(stdout);
	m_pos = 0;
	m_stime = clock();
}

void ConsoleProgressBar::endTask() {
	
}

void ConsoleProgressBar::update(int n, int max) {
	while((n*PROGRESS_BAR_WIDTH) / max > m_pos) {
		printf(".");
		m_pos++;
	}
	if (n == max) {
		int end_time = clock();
		int secs = (end_time-m_stime)/CLOCKS_PER_SEC;
		printf(" %3dm%02ds\n", secs/60, secs%60);
	}
	fflush(stdout);
}