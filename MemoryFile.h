#ifndef _MEMORY_FILE_H_
#define _MEMORY_FILE_H_

class MemoryFile {
	char*	m_data;
	int		m_size;
public:
	MemoryFile(const char* filename);
	~MemoryFile();

	int getSize() const;
	const char* getPtr() const;
};

#endif