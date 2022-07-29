#include "AritCode.h"

#include <cstdint>

void AritCodeInit(struct AritState *state, void *dest_ptr)
{
	state->dest_ptr = dest_ptr;
	state->dest_bit = -1;
	state->interval_size = 0x80000000;
	state->interval_min = 0;
}

unsigned int AritCodePos(struct AritState *state)
{
	unsigned int right_prob = (state->interval_size >> 3);
	return (state->dest_bit << 12) + AritSize2(right_prob, 0x20000000 - right_prob) + 1;
}

__forceinline void PutBit(unsigned char* dest_ptr, int dest_bit)
{
	unsigned int msk, v;
	do
	{
		--dest_bit;
		if(dest_bit < 0)
			return;
		msk = 1u << (dest_bit & 7);
		
		v = dest_ptr[dest_bit >> 3];
		dest_ptr[dest_bit >> 3] = v ^ msk;
	} while(v & msk);
}

void AritCode(struct AritState *state, unsigned int zero_prob, unsigned int one_prob, int bit)
{
	unsigned char* dest_ptr = (unsigned char*)state->dest_ptr; 
	unsigned int dest_bit = state->dest_bit;
	unsigned int interval_min = state->interval_min;
	unsigned int interval_size = state->interval_size;
	
	unsigned int total_prob = zero_prob + one_prob;

	unsigned int threshold = (uint64_t)interval_size * zero_prob / total_prob;
	if(bit)
	{
		unsigned int old_interval_min = interval_min;
		interval_min += threshold;
		if(interval_min < old_interval_min)	// Carry
			PutBit(dest_ptr, dest_bit);

		interval_size -= threshold;
	}
	else
	{
		interval_size = threshold;
	}

	while(interval_size < 0x80000000)
	{
		dest_bit++;
		
		if(interval_min & 0x80000000)
			PutBit(dest_ptr, dest_bit);
		interval_min <<= 1;
		interval_size <<= 1;
	}

	state->dest_bit = dest_bit;
	state->interval_min = interval_min;
	state->interval_size = interval_size;
}

int AritCodeEnd(struct AritState *state)
{
	unsigned char* dest_ptr = (unsigned char*)state->dest_ptr;
	unsigned int dest_bit = state->dest_bit;
	unsigned int interval_min = state->interval_min;
	unsigned int interval_size = state->interval_size;

	if (interval_min > 0) {
		// If current bit is not contained in interval, advance bit
		if (interval_min + interval_size - 1 >= interval_min)
		{
			dest_bit++;
		}
		PutBit(dest_ptr, dest_bit);
	}

	return dest_bit;
}
