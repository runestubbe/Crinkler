#include "Model.h"

#include <nmmintrin.h>

unsigned int ModelHashStart(unsigned int mask)
{
	unsigned char dl = mask;
	unsigned int hash = mask;

	hash = _mm_crc32_u8(hash, 0);

	while(dl)
	{
		if((dl & 0x80))
			hash = _mm_crc32_u8(hash, 0);
		dl += dl;
	}
	return hash;
}

unsigned int ModelHash(const unsigned char* data, int bitpos, unsigned int mask)
{
	unsigned char dl = mask;
	const unsigned char* ptr = data + (bitpos >> 3);
	
	unsigned int hash = mask;
	unsigned char current_byte = (0x100 | *ptr) >> ((~bitpos & 7) + 1);
	hash = _mm_crc32_u8(hash, current_byte);
	while(dl != 0)
	{
		ptr--;
		if(dl & 0x80)
		{
			current_byte = *ptr;
			hash = _mm_crc32_u8(hash, current_byte);
		}
		dl += dl;
	}
	return hash;
}
