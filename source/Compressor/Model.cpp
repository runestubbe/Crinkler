#include "model.h"

unsigned int ModelHashStart(unsigned int mask, int hashmul)
{
	unsigned char dl = mask;
	unsigned int hash = mask;

	hash = hash * hashmul - 1;

	while(dl)
	{
		if((dl & 0x80))
			hash = hash * hashmul - 1;
		dl += dl;
	}
	return hash;
}

unsigned int ModelHash(const unsigned char* data, int bitpos, unsigned int mask, int hashmul)
{
	unsigned char dl = mask;
	const unsigned char* ptr = data + (bitpos >> 3);
	
	unsigned int hash = mask;
	unsigned char current_byte = (0x100 | *ptr) >> ((~bitpos & 7) + 1);
	hash ^= current_byte;
	hash *= hashmul;
	hash = (hash & 0xFFFFFF00) | ((hash + current_byte) & 0xFF);
	hash--;
	while(dl != 0)
	{
		ptr--;
		if(dl & 0x80)
		{
			current_byte = *ptr;
			hash ^= current_byte;
			hash *= hashmul;
			hash = (hash & 0xFFFFFF00) | ((hash + current_byte) & 0xFF);
			hash--;
		}
		dl += dl;
	}
	return hash;
}
