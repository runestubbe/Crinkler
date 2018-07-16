#pragma once
#ifndef _ARITCODE_H_
#define _ARITCODE_H_

const int BITPREC = 256;
const int BITPREC_TABLE = 4096;

#include <cassert>
#include <cstdlib>
#include <algorithm>

using namespace std;

struct AritState {
  void *dest_ptr;
  unsigned int dest_bit;
  unsigned int interval_size;
  unsigned int interval_min;
};

extern "C" {
	void __cdecl AritCodeInit(struct AritState *state, void *dest_ptr);
	void __cdecl AritCode(struct AritState *state, unsigned int zero_prob, unsigned int one_prob, int bit);
	unsigned int __cdecl AritCodePos(struct AritState *state);
	int __cdecl AritCodeEnd(struct AritState *state);

	void __cdecl AritDecodeInit(struct AritState *state, void *dest_ptr);
	int __cdecl AritDecode(struct AritState *state, int zero_prob, int one_prob);

	unsigned int __cdecl AritSize(int right_prob, int wrong_prob);
	extern int LogTable[];
	void __cdecl breakpoint();
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
	right_len = max(right_len - 12, 0);
	total_len = max(total_len - 12, 0);
	return LogTable[total_prob >> total_len] - LogTable[right_prob >> right_len] + ((total_len - right_len) << 12);
}



#endif
