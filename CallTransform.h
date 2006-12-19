#ifndef _CALL_TRANSFORM_H_
#define _CALL_TRANSFORM_H_

class Hunk;
class CallTransform {
public:
	Hunk* getDetransformHunk();

	int transform(unsigned char* data, int size);

	//Hunk* linkWithTransforms(HunkList& hunks);
};

#endif
