#include <windows.h>
#include <cstdio>
#include <ppl.h>
#include "Compressor.h"
#include "CompressionState.h"
#include "SoftwareCompressionStateEvaluator.h"
#include "ModelList.h"
#include "aritcode.h"
#include "model.h"

const unsigned int MAX_N_MODELS = 21;
const unsigned int MAX_MODEL_WEIGHT = 9;

BOOL APIENTRY DllMain( HANDLE, 
                       DWORD, 
                       LPVOID
					 )
{
    return TRUE;
}


const char *compTypeName(CompressionType ct)
{
	switch(ct) {
		case COMPRESSION_INSTANT:
			return "INSTANT";
		case COMPRESSION_FAST:
			return "FAST";
		case COMPRESSION_SLOW:
			return "SLOW";
	}
	return "UNKNOWN";
}


unsigned char swapBitsInByte(unsigned char m) {
	m = (unsigned char) (((m&0x0f)<<4)|((m&0xf0)>>4));
	m = (unsigned char) (((m&0x33)<<2)|((m&0xcc)>>2));
	m = (unsigned char) (((m&0x55)<<1)|((m&0xaa)>>1));
	return m;
}

unsigned int approximateWeights(CompressionState& cs, ModelList& models) {
	for (int i = 0 ; i < models.nmodels ; i++) {
		unsigned char w = 0;
		for (int b = 0 ; b < 8 ; b++) {
			if (models[i].mask & (1 << b)) {
				w++;
			}
		}
		models[i].weight = w;
	}
	return cs.setModels(models);
}

unsigned int optimizeWeights(CompressionState& cs, ModelList& models) {
	ModelList newmodels(models);
	int index = models.nmodels-1;
	int dir = 1;
	int lastindex = index;
	unsigned int size;
	unsigned int bestsize = approximateWeights(cs, models);
	
	if(models.nmodels == 0)	//nothing to optimize, leave and prevent a crash
		return bestsize;

	do {
		int skip = 0;
		for (int i = 0 ; i < models.nmodels; i++) {
			newmodels[i].weight = models[i].weight;
			newmodels[i].mask = models[i].mask;

			if (i == index) {
				newmodels[i].weight += dir;
				// cap weight
				if(newmodels[i].weight > MAX_MODEL_WEIGHT) {
					newmodels[i].weight = MAX_MODEL_WEIGHT;
					skip = 1;
				}
				if (newmodels[i].weight == 255) {
					newmodels[i].weight = 0;
					skip = 1;
				}
			}
		}
		if (!skip) {
			size = cs.setModels(newmodels);//calcSize(prep, bitlength, baseprob, newmodels, nmodels);
		}
		if (!skip && size < bestsize) {
			bestsize = size;
			for (int i = 0 ; i < models.nmodels ; i++) {
				models[i].weight = newmodels[i].weight;
			}
			lastindex = index;
		} else {
			if (dir == 1 && models[index].weight > 0) {
				dir = -1;
			} else {
				dir = 1;
				index--;
				if (index == -1) {
					index = models.nmodels-1;
				}
				if (index == lastindex) break;
			}
		}
	} while (1);

	return bestsize;
}

unsigned int tryWeights(CompressionState& cs, ModelList& models, int bestsize, CompressionType compressionType) {
	unsigned int size;
	switch (compressionType) {
	case COMPRESSION_FAST:
		size = approximateWeights(cs, models);
		break;
	case COMPRESSION_SLOW:
	case COMPRESSION_VERYSLOW:
		size = optimizeWeights(cs, models);
		break;
	}
	size += 8*BITPREC*models.nmodels;
	return size;
}

ModelList InstantModels() {
	ModelList models;
	models[0].mask = 0x00;	models[0].weight = 0;
	models[1].mask = 0x80;	models[1].weight = 2;
	models[2].mask = 0x40;	models[2].weight = 1;
	models[3].mask = 0xC0;	models[3].weight = 3;
	models[4].mask = 0x20;	models[4].weight = 0;
	models[5].mask = 0xA0;	models[5].weight = 2;
	models[6].mask = 0x60;	models[6].weight = 2;
	models[7].mask = 0x90;	models[7].weight = 2;
	models[8].mask = 0xFF;	models[8].weight = 7;
	models[9].mask = 0x51;	models[9].weight = 2;
	models[10].mask = 0xB0;	models[10].weight = 3;
	models.nmodels = 11;
	return models;
}

ModelList ApproximateModels(const unsigned char* data, int datasize, int baseprob, int* compsize, ProgressBar* progressBar, bool verbose, CompressionType compressionType) {
	unsigned char masks[256];
	int m;
	int mask;
	unsigned int size, bestsize;
	int maski;

	ModelList models;
	SoftwareCompressionStateEvaluator evaluator;

	CompressionState cs(data, datasize, baseprob, &evaluator);

	for (m = 0 ; m <= 255 ; m++) {
		mask = m;
		mask = ((mask&0x0f)<<4)|((mask&0xf0)>>4);
		mask = ((mask&0x33)<<2)|((mask&0xcc)>>2);
		mask = ((mask&0x55)<<1)|((mask&0xaa)>>1);
		masks[m] = (unsigned char)mask;
	}

	bestsize = cs.getCompressedSize();

	for (maski = 0 ; maski <= 255 ; maski++) {
		int used = 0;
		mask = masks[maski];

		for (m = 0 ; m < models.nmodels ; m++) {
			if (models[m].mask == mask) {
				used = 1;
			}
		}

		if (!used && models.nmodels < MAX_N_MODELS) {
			models[models.nmodels].mask = (unsigned char)mask;
			models[models.nmodels].weight = 0;
			models.nmodels++;

			size = tryWeights(cs, models, bestsize, compressionType);

			if (size < bestsize) {
				bestsize = size;

				// Try remove
				for (m = models.nmodels-2 ; m >= 0 ; m--) {
					Model rmod = models[m];
					models.nmodels -= 1;
					models[m] = models[models.nmodels];
					size = tryWeights(cs, models, bestsize, compressionType);
					if (size < bestsize) {
						bestsize = size;
					} else {
						models[m] = rmod;
						models.nmodels++;
					}
				}
			} else {
				// Try replace
				models.nmodels--;
				if (compressionType == COMPRESSION_VERYSLOW) {
					for (m = models.nmodels-1 ; m >= 0 ; m--) {
						Model rmod = models[m];
						models[m] = models[models.nmodels];
						size = tryWeights(cs, models, bestsize, compressionType);
						if (size < bestsize) {
							bestsize = size;
							break;
						} else {
							models[m] = rmod;
						}
					}
				}
			}
		}

		
		if(progressBar)
			progressBar->update(maski+1, 256);
	}

	size = optimizeWeights(cs, models);
	if(compsize)
		*compsize = size;

	float bytesize = size / (float) (BITPREC * 8);
	printf("Ideal compressed size: %.2f\n", bytesize);
	if (verbose) {
		models.print();
	}
	return models;
}

int compress1K(unsigned char* data, int size, unsigned char* compressed, int compressed_size, int* modeldata, int b0, int b1, int boost_factor, unsigned int modelmask, int* sizefill, int* internal_size) {
	AritState as;
	memset(compressed, 0, compressed_size);	
	AritCodeInit(&as, compressed);
	int bitlength = size*8;
	for(int i = 0; i < bitlength; i++) {
		int bitpos = (i & 7);
		int bytepos = i >> 3;
		int mask = 0xFF00 >> bitpos;
		int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;

		int n[2];
		if(bit) {
			n[0] = b0; n[1] = b1;
		} else {
			n[0] = b1; n[1] = b0;
		}
		unsigned int mmask = modelmask;
#if 1
		while(mmask != 0)
		{
			DWORD idx;
			BitScanForward(&idx, mmask);
			int m = idx;
			int c[2] = {modeldata[(bitlength*m+i)*2], modeldata[(bitlength*m+i)*2+1]};
			if(c[0]*c[1] == 0) {
				c[0]*=boost_factor;
				c[1]*=boost_factor;
			}
			n[0] += c[0];
			n[1] += c[1];
			mmask &= (mmask-1);
		}
#else
		for(int m = 31; m >= 0; m--) {
			if(mmask & 0x80000000) {
				int c[2] = {modeldata[(bitlength*m+i)*2], modeldata[(bitlength*m+i)*2+1]};
				if(c[0]*c[1] == 0) {
					c[0]*=boost_factor;
					c[1]*=boost_factor;
				}
				n[0] += c[0];
				n[1] += c[1];
			}
			mmask *= 2;
		}
#endif

		if((i & 7) == 0 && sizefill)
			*sizefill++ = AritCodePos(&as)/(BITPREC_TABLE/BITPREC);
		AritCode(&as, n[1-bit], n[bit], 1-bit);
	}

	if(sizefill)
		*sizefill++ = AritCodePos(&as)/(BITPREC_TABLE/BITPREC);
	if(internal_size)
		*internal_size = AritCodePos(&as)/(BITPREC_TABLE/BITPREC);

	return (AritCodeEnd(&as) + 7) / 8;
}

void TinyCompress(unsigned char* org_data, int size, unsigned char* compressed, int& compressed_size,
				  int& best_boost, int& best_b0, int& best_b1, unsigned int& best_modelmask, int* sizefill) {
	unsigned char* data = new unsigned char[size+8];
	memset(data, 0, 8);
	data += 8;
	memcpy(data, org_data, size);

	const int NUM_MODELS = 32;
	int bitlength = size*8;
	int* modeldata = new int[bitlength*NUM_MODELS*2];

	//collect model data
	for(int i = 0; i < bitlength; i++) {
		int bitpos = (i & 7);
		int bytepos = i >> 3;
		int mask = 0xFF00 >> bitpos;
		int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;
	
		for(int model = NUM_MODELS-1; model >= 0; model--) {
			int c[2] = {0,0};
			int startpos = 0;
			int offset = bytepos;

			do {
				if((data[startpos] & mask) == (data[startpos+offset] & mask)) {
					bool match = true;
					int prediction_bit;
					if(offset == 0)
						prediction_bit = 0;
					else
						prediction_bit = ((data[startpos] << bitpos) & 0x80) == 0x80;

					int m = model;
					int k = 1;
					while(m) {
						if(m & 1) {
							if(data[startpos-k] != data[startpos+offset-k]) {
								match = false;
								break;
							}
						}
						k++;
						m >>= 1;
					}

					if(match) {
						c[prediction_bit]++;
						c[!prediction_bit] = (c[!prediction_bit]+1) / 2;
					}
				}

				startpos++;
				offset--;
			} while(offset > 0);

			modeldata[(bitlength*model+i)*2] = c[bit];
			modeldata[(bitlength*model+i)*2+1] = c[!bit];
		}
	}

	int best_size = INT_MAX;

	best_modelmask = 0xFFFFFFFF;
#if 1
	int best_exclude;
	do {
		best_exclude = -1;
		unsigned int prev_best_modelmask = best_modelmask;
#if USE_OPENMP
		#pragma omp parallel for
		for(int i = 0; i < 32; i++)
#else
		concurrency::critical_section cs;
		concurrency::parallel_for(0, 31, [&](int i)
#endif
		{
			unsigned int modelmask = prev_best_modelmask ^ (1<<i);
			for(int boost_factor = 3; boost_factor < 6; boost_factor++) {
				for(int b0 = 3; b0 < 7; b0++) {
					int testsize;
					for(int b1 = 3; b1 < 7; b1++) {
						compress1K(data, size, compressed, compressed_size,
							modeldata, b0, b1, boost_factor, modelmask, NULL, &testsize);
#if USE_OPENMP
						#pragma omp critical (update)
#else
						Concurrency::critical_section::scoped_lock l(cs);
#endif
						{
							if(testsize < best_size) {
								best_size = testsize;
								best_boost = boost_factor;
								best_b0 = b0;
								best_b1 = b1;
								best_modelmask = modelmask;
								best_exclude = i;
								printf("baseprob: (%d, %d) boost: %d modelmask: %8X compressed size: %f bytes\n", best_b0, best_b1, best_boost, best_modelmask, best_size / float(BITPREC*8));
							}
						}
					}
				}
			}
#if USE_OPENMP
		}
#else
		});
#endif
	} while(best_exclude != -1);
#else
	best_b0 = 4;
	best_b1 = 5;
	best_boost = 5;
	//best_modelmask = 0xAEBABAEF;//0x4EAA3AEF;
	best_modelmask = 0x4EAA3AEF|0x80000000;
#endif


	printf("best:\n");
	printf("baseprob: (%d, %d) boost: %d modelmask: %8X compressed size: %f bytes\n", best_b0, best_b1, best_boost, best_modelmask, best_size / float(BITPREC*8));
	int testsize;

	compressed_size = compress1K(data, size, compressed, compressed_size, modeldata, best_b0, best_b1, best_boost, best_modelmask, sizefill, &testsize);

	best_modelmask <<= 1;

	delete[] modeldata;

	data -= 8;
	delete[] data;
}