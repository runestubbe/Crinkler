#include "Model.h"

#include <nmmintrin.h>

unsigned int ModelHashStart(unsigned int mask)
{
	unsigned int edx = mask;
	unsigned int hash = mask;

	hash = _mm_crc32_u8(hash, 0);

	while(edx)
	{
		if(edx & 1)
			hash = _mm_crc32_u8(hash, 0);
		edx >>= 1;
	}
	return hash;
}

unsigned int ModelHash(const unsigned char* data, int bitpos, unsigned int mask)
{
	unsigned int edx = mask;
	const unsigned char* ptr = data + (bitpos >> 3);
	
	unsigned int hash = mask;
	unsigned char current_byte = (0x100 | *ptr) >> ((~bitpos & 7) + 1);
	hash = _mm_crc32_u8(hash, current_byte);
	while(edx != 0)
	{
		ptr--;
		if(edx & 1)
		{
			current_byte = *ptr;
			hash = _mm_crc32_u8(hash, current_byte);
		}
		edx >>= 1;
	}
	return hash;
}
