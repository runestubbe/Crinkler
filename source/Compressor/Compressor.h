#pragma once
#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

static const int MAX_INPUT_SIZE = 128000;
static const int MAX_CONTEXT_LENGTH = 8;
static const int LOG2_NUM_PACKAGE_VECTORS = 4;
static const int NUM_PACKAGE_VECTORS = 1 << LOG2_NUM_PACKAGE_VECTORS;

static const int PACKAGE_SIZE = NUM_PACKAGE_VECTORS * 4;

enum CompressionType {COMPRESSION_INSTANT, COMPRESSION_FAST, COMPRESSION_SLOW, COMPRESSION_VERYSLOW};

#include "aritcode.h"
#include "CompressionStream.h"
#include "ModelList.h"

// Approximates the models for a given data chunk
typedef void (ProgressCallback)(void* userData, int value, int max);
ModelList1k ApproximateModels1k(const unsigned char* data, int datasize, int* compsize, ProgressCallback* progressCallback, void* progressUserData);
ModelList ApproximateModels4k(const unsigned char* data, int datasize, int baseprob, bool saturate, int* compsize, CompressionType compressionType, char* context, ProgressCallback* progressCallback, void* progressUserData);
ModelList InstantModels();

int EvaluateSize(const unsigned char* data, int rawsize, int splittingPoint,
	const ModelList& models1, const ModelList& models2, int baseprob, bool saturate,
	int* out_size1, int* out_size2);

int Compress(unsigned char* compressed, int* sizefill, int maxsize, bool saturate,
	const unsigned char* data, int rawsize, int splittingPoint,
	const ModelList& models1, const ModelList& models2, int baseprob, int hashsize);

int CompressFromHashBits(unsigned char* compressed, int* sizefill, TinyHashEntry* hashtable1, TinyHashEntry* hashtable2, int maxsize, bool saturate,
	const HashBits& hashbits1, const HashBits& hashbits2, int baseprob, int hashsize);

int Compress1K(unsigned char* data, int size, unsigned char* compressed, int compressed_size, int boost_factor, int b0, int b1, unsigned int modelmask, int* sizefill, int* internal_size);

const char *compTypeName(CompressionType ct);

#endif
