#ifndef _CALL_TRANSFORM_H_
#define _CALL_TRANSFORM_H_

#include "Transform.h"

class Hunk;
class CallTransform : public Transform {
	bool disabled;
public:
	Hunk* GetDetransformer();
	bool DoTransform(Hunk* hunk, int codeSize, bool verbose);

	int GetFlags();
};

#endif
