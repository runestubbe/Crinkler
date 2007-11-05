#include "misc.h"
#include <cmath>

int align(int v, int alignmentBits) {
	int mask = (1<<alignmentBits)-1;
	return ((v + mask) >> alignmentBits)<<alignmentBits;
}

float roundFloat(float ptr, int bits) {
	int truncBits = 32-bits;
	float* v = &ptr;
	double orgv = *v;
	unsigned int* iv = (unsigned int*)v;

	if(bits < 2) {
		*iv = 0;
	} else {
		//round
		//compare actual floating point values due to non-linearity
		*iv >>= truncBits;
		*iv <<= truncBits;
		if(truncBits > 0) {
			double v0 = *v;
			(*iv) += 1<<truncBits;
			double v1 = *v;
			if(fabs(orgv-v0) < fabs(orgv-v1)) {
				(*iv) -= 1<<truncBits;
			}
		}
	}
	return *v;
}
