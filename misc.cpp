#include "misc.h"

int align(int v, int alignmentBits) {
	int mask = (1<<alignmentBits)-1;
	return ((v + mask) >> alignmentBits)<<alignmentBits;
}