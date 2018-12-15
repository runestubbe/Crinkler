#include <cstdio>

#include "MemoryFile.h"

#include "Log.h"

MemoryFile::MemoryFile(const char* filename, bool abort_if_failed) {
	FILE* file;
	if(!fopen_s(&file, filename, "rb")) {
		fseek(file, 0, SEEK_END);
		int filesize = ftell(file);
		fseek(file, 0, SEEK_SET);
		m_data = new char[filesize+2];
		m_data[filesize] = 0;
		m_data[filesize+1] = 0;
		fread(m_data, 1, filesize, file);
		fclose(file);
		m_size = filesize;
	} else {
		if (abort_if_failed) {
			Log::error("", "Cannot open file '%s'\n", filename);
		}
		m_data = NULL;
		m_size = 0;
	}
}


MemoryFile::~MemoryFile() {
	delete[] m_data;
}


char* MemoryFile::getPtr() const {
	return m_data;
}


int MemoryFile::getSize() const {
	return m_size;
}

bool MemoryFile::write(const char *filename) const {
	FILE *file;
	fopen_s(&file, filename, "wb");
	if (!file) return false;
	fwrite(m_data, 1, m_size, file);
	fclose(file);
	return true;
}
