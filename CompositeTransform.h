#pragma once
#ifndef _COMPOSITE_TRANSFORM_H_
#define _COMPOSITE_TRANSFORM_H_

#include "Transform.h"

#include <vector>

class CompositeTransform : public Transform {
	std::vector<Transform*> m_transforms;
public:
	Hunk* getDetransformer();
	void transform(Hunk* hunk, int splittingPoint);

	void addTransform(Transform* trans);
};

#endif
