#include "misc.h"

int Align(int v, int alignmentBits) {
	int mask = (1<<alignmentBits)-1;
	return ((v + mask) >> alignmentBits)<<alignmentBits;
}

unsigned long long RoundInt64(unsigned long long v, int bits) {
	if(bits == 0)
		return 0;
	if(bits == 64)
		return v;
	unsigned long long remainder = v << bits;
	unsigned long long quotient = v >> (64-bits);
	if(remainder >= 0x8000000000000000) {
		quotient++;
	}
	return quotient<<(64-bits);
}

int NumberOfModelsInWeightMask(unsigned int mask) {
	int n = -1;
	do {
		while(mask & 0x80000000)
			mask <<= 1;
		mask <<= 1;
		n++;
	} while(mask);
	return n;
}
