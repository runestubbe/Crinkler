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

static const int NUM_1K_MODELS = 33;	//31 is implicitly always enabled. 30 to -1 are optional
static const int MIN_1K_BASEPROB = 4;
static const int MAX_1K_BASEPROB = 8;
static const int NUM_1K_BASEPROBS = MAX_1K_BASEPROB - MIN_1K_BASEPROB + 1;

static const int MIN_1K_BOOST_FACTOR = 4;
static const int MAX_1K_BOOST_FACTOR = 10;
static const int NUM_1K_BOOST_FACTORS = MAX_1K_BOOST_FACTOR - MIN_1K_BOOST_FACTOR + 1;

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

ModelList ApproximateModels4k(const unsigned char* data, int datasize, int baseprob, bool saturate, int* compsize, ProgressBar* progressBar, bool verbose, CompressionType compressionType) {
	unsigned char masks[256];
	int m;
	int mask;
	unsigned int size, bestsize;
	int maski;

	ModelList models;
	SoftwareCompressionStateEvaluator evaluator;

	CompressionState cs(data, datasize, baseprob, saturate, &evaluator);

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

static int* GenerateModelData1k(const unsigned char* org_data, int datasize)
{
	unsigned char* data = new unsigned char[datasize + 16];
	memset(data, 0, 16);
	data += 16;
	memcpy(data, org_data, datasize);

	int bitlength = datasize * 8;
	int* modeldata = new int[bitlength*NUM_1K_MODELS * 2];

	//collect model data
	struct SHashEntry
	{
		unsigned int hash;
		int c[2];
		unsigned char model;
		unsigned char bitpos;
		short bytepos;
		
	};
	const int hash_table_size = bitlength*NUM_1K_MODELS * 2;
	SHashEntry* hash_table = new SHashEntry[hash_table_size];
	for (int i = 0; i < hash_table_size; i++)
	{
		SHashEntry& entry = hash_table[i];
		entry.model = 0;
		entry.bitpos = 0;
		entry.bytepos = 0x7FFF;
		entry.c[0] = 0;
		entry.c[1] = 0;
	}

	for(int bytepos = -1; bytepos < datasize; bytepos++)
	{
		for(int bitpos = 0; bitpos < 8; bitpos++)
		{

			int mask = 0xFF00 >> bitpos;
			int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;

			for(int model_idx = 0; model_idx < NUM_1K_MODELS; model_idx++)
			{
				int model = (unsigned char)(model_idx - 1);
				// calculate hash
				unsigned int hash = bitpos + (data[bytepos] & mask) * 8;
				int m = model;
				int k = 1;
				while(m)
				{
					hash = hash * 237 + 4123;
					if(m & 1)
					{
						hash += data[bytepos - k];
					}

					k++;
					m >>= 1;
				}

				unsigned int entry_idx = hash % hash_table_size;
				SHashEntry* entry_ptr = NULL;
				while(true)
				{
					entry_ptr = &hash_table[entry_idx];
					if(entry_ptr->bytepos == 0x7FFF)
					{
						entry_ptr->hash = hash;
						entry_ptr->model = model;
						entry_ptr->bitpos = bitpos;
						entry_ptr->bytepos = bytepos;
						entry_ptr->c[0] = 0;
						entry_ptr->c[1] = 0;
						break;
					}
					else
					{
						// does it match?
						bool match = false;

						if(entry_ptr->hash == hash && entry_ptr->bitpos == bitpos && entry_ptr->model == model && (data[entry_ptr->bytepos] & mask) == (data[bytepos] & mask))
						{
							match = true;

							int m = model;
							int k = 1;
							while(m)
							{
								if(m & 1)
								{
									if(data[entry_ptr->bytepos - k] != data[bytepos - k])
									{
										match = false;
										break;
									}
								}
								k++;
								m >>= 1;
							}
						}

						if(match)
						{
							break;
						}
						else
						{
							entry_idx++;
							if(entry_idx >= (unsigned int)hash_table_size) entry_idx = 0;
						}
					}
				}

				if(bytepos >= 0)
				{
					int i = bytepos * 8 + bitpos;
					modeldata[(bitlength*model_idx + i) * 2] = entry_ptr->c[bit];
					modeldata[(bitlength*model_idx + i) * 2 + 1] = entry_ptr->c[1 - bit];
				}
				
				entry_ptr->c[bit]++;
				entry_ptr->c[!bit] = (entry_ptr->c[!bit] + 1) / 2;
			}
		}

	}
	
	delete[] hash_table;
	
	data -= 16;
	delete[] data;

	return modeldata;
}

static int previousPowerOf2(int v) {
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	return v + 1;
}

int Compress1K(unsigned char* org_data, int datasize, unsigned char* compressed, int compressed_size, int boost_factor, int b0, int b1, unsigned int modelmask, int* sizefill, int* internal_size)
{
	unsigned char* data = new unsigned char[datasize + 16];
	memset(data, 0, 16);
	data += 16;
	memcpy(data, org_data, datasize);

	//collect model data
	struct SHashEntry
	{
		unsigned int hash;
		int c[2];
		unsigned char model;
		unsigned char bitpos;
		short bytepos;
	};

	struct SEncodeEntry
	{
		int n[2];
	};
	SEncodeEntry* encode_entries = new SEncodeEntry[8 * datasize];	//zero
	memset(encode_entries, 0, 8 * datasize * sizeof(SEncodeEntry));

	const int hash_table_size = previousPowerOf2(datasize * 2);

	SHashEntry* hash_table_data = new SHashEntry[hash_table_size * 8];	//TODO: reuse memory to save ~25% perf
	
	//for (int bitpos = 0; bitpos < 8; bitpos++)
	concurrency::parallel_for(0, 8, [&](int bitpos)
	{
		int mask = 0xFF00 >> bitpos;
		
		for (int model_idx = 0; model_idx < NUM_1K_MODELS; model_idx++)
		{
			if (model_idx != 32 && (modelmask & (1 << model_idx)) == 0)
				continue;

			int model = (unsigned char)(model_idx - 1);

			SHashEntry* hash_table = &hash_table_data[bitpos * hash_table_size];
			for (int i = 0; i < hash_table_size; i++)
			{
				SHashEntry& entry = hash_table[i];
				entry.model = 0;
				entry.bitpos = 0;
				entry.bytepos = 0x7FFF;
				entry.c[0] = 0;
				entry.c[1] = 0;
			}

			for (int bytepos = -1; bytepos < datasize; bytepos++)
			{
				int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;

				unsigned int mmask = model;

				int c[2] = { 0, 0 };

				// calculate hash
				unsigned int hash = (bitpos + (data[bytepos] & mask) * 8) * 4112361;
				{
					int m = model;
					int k = 1;
					while (m)
					{
						hash = hash * 237 + 4123;
						if (m & 1)
						{
							hash += data[bytepos - k];
						}

						k++;
						m >>= 1;
					}		
				}

				unsigned int entry_idx = hash & (hash_table_size - 1);

				SHashEntry* entry_ptr = NULL;
				while (true)
				{
					entry_ptr = &hash_table[entry_idx];
					if(entry_ptr->bytepos == 0x7FFF)
					{
						entry_ptr->hash = hash;
						entry_ptr->model = model;
						entry_ptr->bitpos = bitpos;
						entry_ptr->bytepos = bytepos;
						entry_ptr->c[0] = 0;
						entry_ptr->c[1] = 0;
						break;
					}
					else
					{
						// does it match?
						bool match = false;

						if (entry_ptr->hash == hash && entry_ptr->bitpos == bitpos && entry_ptr->model == model && (data[entry_ptr->bytepos] & mask) == (data[bytepos] & mask))
						{
							match = true;

							int m = model;
							int k = 1;
							while (m)
							{
								if (m & 1)
								{
									if (data[entry_ptr->bytepos - k] != data[bytepos - k])
									{
										match = false;
										break;
									}
								}
								k++;
								m >>= 1;
							}
						}

						if (match)
						{
							break;
						}
						else
						{
							entry_idx++;
							if (entry_idx >= (unsigned int)hash_table_size) entry_idx = 0;
						}
					}
				}

				c[0] = entry_ptr->c[bit];
				c[1] = entry_ptr->c[1 - bit];

				entry_ptr->c[bit]++;
				entry_ptr->c[1 - bit] = (entry_ptr->c[1 - bit] + 1) / 2;
			
				if (c[0] == 0 || c[1] == 0)
				{
					c[0] *= boost_factor;
					c[1] *= boost_factor;
				}
				
				if(bytepos >= 0)
				{
					SEncodeEntry& entry = encode_entries[bitpos*datasize + bytepos];
					entry.n[0] += c[0];
					entry.n[1] += c[1];
				}
			}
		}
	}
	);


	delete[] hash_table_data;

	AritState as;
	memset(compressed, 0, compressed_size);
	AritCodeInit(&as, compressed);

	for (int bytepos = 0; bytepos < datasize; bytepos++)
	{
		if (sizefill)
		{
			*sizefill++ = AritCodePos(&as) / (BITPREC_TABLE / BITPREC);
		}

		for (int bitpos = 0; bitpos < 8; bitpos++)
		{
			int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;
			const SEncodeEntry& entry = encode_entries[bitpos*datasize + bytepos];
			int n[2] = { entry.n[0], entry.n[1] };
			if (bit)
			{
				n[0] += b0; n[1] += b1;
			}
			else
			{
				n[0] += b1; n[1] += b0;
			}

			AritCode(&as, n[1 - bit], n[bit], 1 - bit);
		}
	}

	delete[] encode_entries;

	data -= 16;
	delete[] data;

	if (sizefill)
	{
		*sizefill++ = AritCodePos(&as) / (BITPREC_TABLE / BITPREC);
	}

	if (internal_size)
	{
		*internal_size = AritCodePos(&as) / (BITPREC_TABLE / BITPREC);
	}

	return (AritCodeEnd(&as) + 7) / 8;
}


int evaluate1K(unsigned char* data, int size, int* modeldata, int* out_b0, int* out_b1, int* out_boost_factor, unsigned int modelmask)
{
	int bitlength = size * 8;
	int totalsizes[NUM_1K_BOOST_FACTORS][NUM_1K_BASEPROBS][NUM_1K_BASEPROBS] = {};

	for (int i = 0; i < bitlength; i++)
	{
		int bitpos = (i & 7);
		int bytepos = i >> 3;
		int mask = 0xFF00 >> bitpos;
		int bit = ((data[bytepos] << bitpos) & 0x80) == 0x80;

		int n[2][2] = {};	//boost_n0, boost_n1, no_boost_n0, no_boost_n1

		unsigned int mmask = modelmask;

		int model = 31;
		
		{
			lala:
			int model_idx = model + 1;
			int c[2] = { modeldata[(bitlength*model_idx + i) * 2], modeldata[(bitlength*model_idx + i) * 2 + 1] };
			int boost = (c[0] * c[1] == 0);
			n[boost][0] += c[0];
			n[boost][1] += c[1];
			
		skip:
			bool handle = (((unsigned int)mmask) & 0x80000000) != 0;
			if(mmask == 0)
				goto end;
			mmask <<= 1;

			
			model--;
			if(handle)
				goto lala;
			else
				goto skip;
		end:
			;
		}

		for (int boost_idx = 0; boost_idx < NUM_1K_BOOST_FACTORS; boost_idx++)
		{
			int boost_factor = boost_idx + MIN_1K_BOOST_FACTOR;
			int total_n0 = n[0][0] + n[1][0] * boost_factor;
			int total_n1 = n[0][1] + n[1][1] * boost_factor;
			for (int b1 = 0; b1 < NUM_1K_BASEPROBS; b1++)
			{
				for (int b0 = 0; b0 < NUM_1K_BASEPROBS; b0++)
				{
					int n0, n1;
					if (bit)
					{
						n0 = total_n0 + b0 + MIN_1K_BASEPROB; n1 = total_n1 + b1 + MIN_1K_BASEPROB;
					}
					else
					{
						n0 = total_n0 + b1 + MIN_1K_BASEPROB; n1 = total_n1 + b0 + MIN_1K_BASEPROB;
					}
					totalsizes[boost_idx][b1][b0] += AritSize2(n0, n1);
				}
			}
		}
	}

	int min_totalsize = INT_MAX;
	for (int boost_idx = 0; boost_idx < NUM_1K_BOOST_FACTORS; boost_idx++)
	{
		for (int b1 = 0; b1 < NUM_1K_BASEPROBS; b1++)
		{
			for (int b0 = 0; b0 < NUM_1K_BASEPROBS; b0++)
			{
				if (totalsizes[boost_idx][b1][b0] < min_totalsize)
				{
					*out_b0 = b0 + MIN_1K_BASEPROB;
					*out_b1 = b1 + MIN_1K_BASEPROB;
					*out_boost_factor = boost_idx + MIN_1K_BOOST_FACTOR;
					min_totalsize = totalsizes[boost_idx][b1][b0];
				}
			}
		}
	}

	return (min_totalsize / (BITPREC_TABLE / BITPREC));
}

ModelList1k ApproximateModels1k(const unsigned char* org_data, int datasize, int* compsize, ProgressBar* progressBar, bool verbose)
{
	unsigned char* data = new unsigned char[datasize + 16];
	memset(data, 0, 16);
	data += 16;
	memcpy(data, org_data, datasize);

	int* modeldata = GenerateModelData1k(org_data, datasize);

	int best_size = INT_MAX;

	unsigned int best_modelmask = 0xFFFFFFFF;	//note bit 31 must always be set
	unsigned int best_boost = 0;
	unsigned int best_b0 = 0;
	unsigned int best_b1 = 0;

	int max_models = NUM_1K_MODELS - 1;
	int num_models = max_models;

	int best_flip;
	for (int tries = 0; tries < max_models; tries++)
	{
		best_flip = -1;
		unsigned int prev_best_modelmask = best_modelmask;

		concurrency::critical_section cs;
		concurrency::parallel_for(0, num_models, [&](int i)
		{
			int model_idx = 0;
			int bitcount = i;
			while (true)
			{
				if (((prev_best_modelmask >> model_idx) & 1))
				{
					if (bitcount == 0)
					{
						break;
					}
					else
					{
						bitcount--;
					}
				}

				model_idx++;
			}

			assert(((prev_best_modelmask >> model_idx) & 1));
			unsigned int modelmask = prev_best_modelmask ^ (1 << model_idx);

			{
				int boost_factor;
				int testsize;
				int b0, b1;
				testsize = evaluate1K(data, datasize, modeldata, &b0, &b1, &boost_factor, modelmask);

				Concurrency::critical_section::scoped_lock l(cs);
				if (testsize < best_size)
				{
					best_size = testsize;
					best_boost = boost_factor;
					best_b0 = b0;
					best_b1 = b1;
					best_modelmask = modelmask;
					best_flip = i;
					//printf("baseprob: (%d, %d) boost: %d modelmask: %8X compressed size: %f bytes\n", best_b0, best_b1, best_boost, best_modelmask, best_size / float(BITPREC * 8));
				}
			}

		});
		num_models--;

		if (best_flip == -1)
		{
			progressBar->update(1, 1);
			break;
		}

		if (progressBar)
		{
			progressBar->update(tries + 1, max_models);
		}
	}

	delete[] modeldata;

	data -= 16;
	delete[] data;

	ModelList1k model;
	model.modelmask = best_modelmask;
	model.baseprob0 = best_b0;
	model.baseprob1 = best_b1;
	model.boost = best_boost;

	if (compsize)
	{
		*compsize = best_size;
	}

	float bytesize = best_size / (float)(BITPREC * 8);
	printf("Ideal compressed size: %.2f\n", bytesize);
	return model;
}

