#ifndef _IDENTITY_TRANSFORM_H_
#define _IDENTITY_TRANSFORM_H_

#include "Transform.h"

class Hunk;
class IdentityTransform : public Transform {
public:
	Hunk* getDetransformer();
	int getFlags();
	bool transform(Hunk* hunk, int splittingPoint, bool verbose);
};

#endif
