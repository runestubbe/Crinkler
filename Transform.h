#pragma once
#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

class Hunk;
class HunkList;
class Symbol;
class Transform {
public:
	virtual ~Transform() {};
	virtual Hunk* getDetransformer() = 0;
	virtual bool transform(Hunk* hunk, int splittingPoint, bool verbose) = 0;

	//links and transforms a hunklist. provides both a transformed and non-transformed linked version.
	//returns true if the transform succeeds
	bool linkAndTransform(HunkList* hunklist, Symbol *entry_label, int baseAddress, Hunk* &transformedHunk, Hunk** untransformedHunk, int* splittingPoint, bool verbose);
};

#endif
