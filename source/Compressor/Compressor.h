#pragma once
#ifndef _COMPRESSOR_H_
#define _COMPRESSOR_H_

#include "AritCode.h"
#include "CompressionStream.h"
#include "ModelList.h"

static const int MAX_CONTEXT_LENGTH =	8;		// Maximum size of context window
static const int DEFAULT_BASEPROB	=	10;		// Default weight for trivial model
static const int BIT_PRECISION		=	256;	// Number of units per bit

enum CompressionType {COMPRESSION_INSTANT, COMPRESSION_FAST, COMPRESSION_SLOW, COMPRESSION_VERYSLOW};

typedef void	(ProgressCallback)(void* userData, int value, int max);

void			InitCompressor();

const char*		CompressionTypeName(CompressionType ct);

ModelList1k		ApproximateModels1k(const unsigned char* inputData, int inputSize, int* outCompressedSize, ProgressCallback* progressCallback, void* progressUserData);
int				Compress1k(const unsigned char* inputData, int inputSize, unsigned char* outCompressedData, int maxCompressedSize, ModelList1k& modelList, int* sizefill, int* outInternalSize);

ModelList4k		InstantModels4k();
ModelList4k		ApproximateModels4k(const unsigned char* inputData, int inputSize, const unsigned char context[MAX_CONTEXT_LENGTH], CompressionType compressionType, bool saturate, int baseprob, int* outCompressedSize, ProgressCallback* progressCallback, void* progressUserData);
int				EvaluateSize4k(const unsigned char* inputData, int numSegments, const int* segmentSizes, int* outCompressedSegmentSizes, ModelList4k** modelLists, int baseprob, bool saturate);
int				Compress4k(const unsigned char* inputData, int numSegments, const int* segmentSizes, unsigned char* outCompressedData, int maxCompressedSize, ModelList4k** modelLists, bool saturate, int baseprob, int hashsize, int* sizefill);
int				CompressFromHashBits4k(const HashBits* hashbits, TinyHashEntry** hashtables, int numSegments, unsigned char* outCompressedData, int maxCompressedSize, bool saturate, int baseprob, int hashsize, int* sizefill);

#endif
