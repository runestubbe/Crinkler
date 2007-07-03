#include "TinyCompress.h"

#include <cstdio>
#include <cmath>
#include <memory>

using namespace std;

void TinyCompress(unsigned char* org_data, int size) {
	printf("tiny compressing :)\n");
	unsigned char* data = new unsigned char[size+8];
	memset(data, 0, 8);
	data += 8;
	memcpy(data, org_data, size);

	double entropy = 0;
	for(int i = 0; i < size*8; i++) {
		int bitpos = (i & 7);
		int bytepos = i >> 3;
		int mask = 0xFF00 >> bitpos;
		int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;

		int baseprob = 11;
		int n[2] = {baseprob, baseprob};
		for(int model = 0; model < 8; model++) {
			int c[2] = {0,0};
			for(int j = 0; j < bytepos; j++) {
				if(data[j] & mask != data[bytepos] & mask)
					continue;	//first bytes don't match, skip

				bool match = true;
				int prediction_bit = ((data[j] << bitpos) & 0x80) == 0x80;
				int m = model;
				int k = 1;
				while(m) {
					if(m & 1) {
						if(data[j-k] != data[bytepos-k]) {
							match = false;
							break;
						}
					}
					k++;
					m >>= 1;
				}

				if(match) {
					c[prediction_bit]++;
					if(c[!prediction_bit] > 1)
						c[!prediction_bit]>>=1;
				}
			}
			int boost = (c[0]*c[1] == 0)*3;
			n[0] += (c[0] << boost);
			n[1] += (c[1] << boost);
		}

		entropy -= log(n[bit] / (double)(n[0]+n[1]));
	}
	entropy /= log(256.0);

	data -= 8;
	delete[] data;

	printf("entropy: %f\n", entropy);
}

