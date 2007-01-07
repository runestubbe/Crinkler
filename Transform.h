#pragma once
#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

class Hunk;
class HunkList;
class Transform {
public:
	virtual Hunk* getDetransformer() = 0;
	virtual void transform(Hunk* hunk, int splittingPoint) = 0;

	Hunk* linkAndTransform(HunkList* hunklist, int baseAddress, int* splittingPoint = 0);
};

#endif