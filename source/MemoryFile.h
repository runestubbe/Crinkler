#ifndef _MEMORY_FILE_H_
#define _MEMORY_FILE_H_

class MemoryFile {
	char*	m_data;
	int		m_size;
public:
	MemoryFile(const char* filename, bool abort_if_failed = true);
	~MemoryFile();

	int		GetSize() const	{ return m_size; }
	char*	GetPtr() const	{ return m_data; }

	bool	Write(const char *filename) const;
};

#endif
