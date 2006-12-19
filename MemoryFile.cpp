#include <cstdio>
#include <cassert>

#include "MemoryFile.h"

MemoryFile::MemoryFile(const char* filename) {
	FILE* file;
	fopen_s(&file, filename, "rb");
	if(file) {
		fseek(file, 0, SEEK_END);
		int filesize = ftell(file);
		fseek(file, 0, SEEK_SET);
		m_data = new char[filesize+2];
		m_data[filesize] = 0;
		m_data[filesize+1] = 0;
		fread(m_data, 1, filesize, file);
		fclose(file);
	} else {
		m_data = NULL;
		m_size = 0;
	}
}


MemoryFile::~MemoryFile() {
	delete[] m_data;
}


const char* MemoryFile::getPtr() const {
	return m_data;
}


int MemoryFile::getSize() const {
	return m_size;
}