#ifndef _CALL_TRANSFORM_H_
#define _CALL_TRANSFORM_H_

#include "Transform.h"

class Hunk;
class CallTransform : public Transform {
	bool disabled;
public:
	Hunk* getDetransformer();
	int getFlags();
	bool transform(Hunk* hunk, int splittingPoint, bool verbose);
};

#endif
