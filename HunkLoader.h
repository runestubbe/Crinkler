#pragma once
#ifndef _HUNK_LOADER_H_
#define _HUNK_LOADER_H_

class HunkList;
class HunkLoader {
public:
	virtual ~HunkLoader() {};

	virtual bool clicks(const char* data, int size) = 0;
	bool clicksFromFile(const char* filename);

	virtual HunkList* load(const char* data, int size, const char* origin) = 0;
	HunkList* loadFromFile(const char* filename);
};

#endif
