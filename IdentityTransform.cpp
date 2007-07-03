#include "IdentityTransform.h"
#include "Hunk.h"
#include "CoffObjectLoader.h"
#include "data.h"
#include "HunkList.h"
#include "Symbol.h"

Hunk* IdentityTransform::getDetransformer() {
	return NULL;
}

bool IdentityTransform::transform(Hunk* hunk, int splittingPoint) {
	return true;
}