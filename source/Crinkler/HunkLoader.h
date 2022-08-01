#pragma once
#ifndef _HUNK_LOADER_H_
#define _HUNK_LOADER_H_

class PartList;
class HunkLoader {
public:
	virtual ~HunkLoader() {};

	virtual bool	Clicks(const char* data, int size) const = 0;
	virtual bool	Load(PartList& parts, const char* data, int size, const char* module, bool inLibrary = false) = 0;
	bool			LoadFromFile(PartList& parts, const char* filename);
};

#endif
