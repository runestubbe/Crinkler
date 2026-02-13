#pragma once
#ifndef _MODEL_
#define _MODEL_

inline int GetBit(const unsigned char *data, int bitpos) {
	return (data[bitpos >> 3] >> (7 - bitpos & 7)) & 1;
}

unsigned int ModelHashStart(unsigned int mask);
unsigned int ModelHash(const unsigned char* data, int bitpos, unsigned int mask);

#endif