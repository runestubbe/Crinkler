#pragma once
#ifndef _HUNK_LOADER_H_
#define _HUNK_LOADER_H_

class HunkList;
class HunkLoader {
public:
	virtual ~HunkLoader() {};

	virtual bool		Clicks(const char* data, int size) const = 0;
	virtual HunkList*	Load(const char* data, int size, const char* module) = 0;
	HunkList*			LoadFromFile(const char* filename);
};

#endif
