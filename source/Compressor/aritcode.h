#pragma once
#ifndef _ARITCODE_H_
#define _ARITCODE_H_

#include <cassert>
#include <intrin.h>

static const int BITPREC = 256;
static const int BITPREC_TABLE_BITS = 12;
static const int BITPREC_TABLE = 1 << BITPREC_TABLE_BITS;

struct AritState {
  void*			dest_ptr;
  unsigned int	dest_bit;
  unsigned int	interval_size;
  unsigned int	interval_min;
};

void			AritCodeInit(struct AritState *state, void *dest_ptr);
void			AritCode(struct AritState *state, unsigned int zero_prob, unsigned int one_prob, int bit);
unsigned int	AritCodePos(struct AritState *state);
int __cdecl		AritCodeEnd(struct AritState *state);

extern "C"
{
	extern int LogTable[];
}

inline int AritSize2(int right_prob, int wrong_prob) {
	assert(right_prob > 0);
	assert(wrong_prob > 0);

	int right_len, total_len;
	int total_prob = right_prob + wrong_prob;
	if(total_prob < BITPREC_TABLE) {
		return LogTable[total_prob] - LogTable[right_prob];
	}
	_BitScanReverse((unsigned long*)&right_len, right_prob);
	_BitScanReverse((unsigned long*)&total_len, total_prob);
	right_len = right_len > 12 ? (right_len - 12) : 0;
	total_len = total_len > 12 ? (total_len - 12) : 0;
	return LogTable[total_prob >> total_len] - LogTable[right_prob >> right_len] + ((total_len - right_len) << 12);
}

#endif
