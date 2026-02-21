#pragma once
#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

class Hunk;
class PartList;
class Symbol;
class Transform {
	bool m_enabled;
public:
	Transform() : m_enabled(true) {};
	virtual ~Transform() {};

	virtual Hunk*	GetDetransformer() = 0;
	virtual bool	DoTransform(Hunk* hunk, int splittingPoint, bool verbose) = 0;

	// Links and transforms a hunklist. Provides both a transformed and non-transformed linked version.
	// Returns true if the transform succeeds
	bool			LinkAndTransform(PartList& parts, Symbol *entry_label, int baseAddress, Hunk** transformedHunk, Hunk** untransformedHunk, bool verbose);

	void			Disable() { m_enabled = false; }
};

class IdentityTransform : public Transform {
public:
	Hunk*	GetDetransformer() { return nullptr; }
	bool	DoTransform(Hunk* hunk, int splittingPoint, bool verbose) { return true; }
};

#endif
