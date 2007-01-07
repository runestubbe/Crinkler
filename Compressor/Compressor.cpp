#include <windows.h>
#include <cstdio>
#include "Compressor.h"
#include "CompressionState.h"
#include "SoftwareCompressionStateEvaluator.h"
#include "GPUCompressionStateEvaluator.h"
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

unsigned int tryWeights(CompressionState& cs, ModelList& models, int bestsize, CompressionType compressionType, int modelbits) {
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
	size += modelbits*BITPREC*models.nmodels;
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

ModelList ApproximateModels(const unsigned char* data, int datasize, int baseprobs[8], int* compsize, ProgressBar* progressBar, bool verbose, CompressionType compressionType, int modelbits) {
	unsigned char masks[256];
	int i,m;
	int mask;
	unsigned int size, bestsize;
	int maski;

	ModelList models;
	SoftwareCompressionStateEvaluator evaluator;

	CompressionState cs(data, datasize, baseprobs, &evaluator);

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

			size = tryWeights(cs, models, bestsize, compressionType, modelbits);

			if (size < bestsize) {
				bestsize = size;

				// Try remove
				for (m = models.nmodels-2 ; m >= 0 ; m--) {
					Model rmod = models[m];
					models.nmodels -= 1;
					models[m] = models[models.nmodels];
					size = tryWeights(cs, models, bestsize, compressionType, modelbits);
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
						size = tryWeights(cs, models, bestsize, compressionType, modelbits);
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


	printf("Ideal compressed size: %d\n", size/BITPREC/8);
	if (verbose) {
		models.print();
	}
	return models;
}
