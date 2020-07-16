// A minimal example that demonstrates how to use the Compressor library.

// The library currently doesn't provide any decompression functionality and
// there is no way to use the resulting compressed blocks directly with Crinkler,
// so the only practical use of the library is for size estimation. We imagine this
// might still be very helpful in some specific tooling scenarios.

// Tools could display compressed size information directly to users to help them
// optimize content. In some cases tools might even be able to use this information
// to drive domain-specific decisions about data representation and layout.

// Compression is split into a modeling phase and an actual compression phase.
// For size estimation, we recommend using the size estimates from the modeling phase
// instead of the compression phase. They are faster to obtain and provide fractional
// bit sizes, instead of sizes in whole bytes. The modeling phase doesn't account for
// redundancy introduced by hashing, but with reasonable values for /HASHSIZE and
// /HASHTRIES this redundancy is typically negligible.

#define _CRT_SECURE_NO_WARNINGS

#include "../Compressor/Compressor.h"

#include <cstdio>

// Optional progress update callback
void ProgressUpdateCallback(void* userData, int value, int max)
{
	printf(".");
}

int main(int argc, const char* argv[])
{
	if (argc != 2)
	{
		printf("Syntax: CompressorExample filename\n");
		return 1;
	}

	// Open file from first command line argument
	const char* filename = argv[1];
	FILE* file = fopen(filename, "rb");
	if (!file)
	{
		printf("Failed to open file '%s'\n", filename);
		return 1;
	}

	// Read data from file
	printf("Loading file '%s'\n", filename);
	fseek(file, 0, SEEK_END);
	int dataSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char* data = new unsigned char[dataSize];
	fread(data, dataSize, 1, file);
	fclose(file);

	// Initialize compressor
	// Needs to be called before any other call to the compressor
	InitCompressor();

	printf("Uncompressed size: %d bytes\n", dataSize);

	// Calculate models for data
	printf("Calculating models...");

	unsigned char context[MAX_CONTEXT_LENGTH] = {};	// The MAX_CONTEXT_LENGTH bytes in the context window before data. They will not be compressed, but will be use for prediction.
	int compressedSize = 0;							// Resulting compressed size. BIT_PRECISION units per bit.
	ModelList4k modelList = ApproximateModels4k(data, dataSize, context, COMPRESSION_SLOW, false, DEFAULT_BASEPROB, &compressedSize, ProgressUpdateCallback, nullptr);

	printf("\nEstimated compressed size: %.3f bytes\n", compressedSize / float(BIT_PRECISION * 8));
	printf("Selected models: ");
	modelList.Print(stdout);
	printf("\n");

	// Do some transformation to the data.
	printf("Transforming data\n");
	for (int i = 0; i < dataSize / 8; i++) data[i] += 17;
	
	// Use the set of models found earlier to estimate the size of the transformed data.
	// Evaluating size using an existing set of models is much faster than calculating a new set of models with ApproximateModelsX.
	// As long as the transformations are small, the size deltas seen from evaluating using a fixed set of models should
	// be close to what you would see when recalculating the models from scratch.
	// Reusing models allows for more rapid iteration by users or tools.
	ModelList4k* modelLists[] = { &modelList };
	int segmentSizes[] = { dataSize };
	int transformedSize = EvaluateSize4k(data, 1, segmentSizes, nullptr, modelLists, DEFAULT_BASEPROB, false);
	printf("Estimated compressed size of transformed data: %.3f bytes\n", transformedSize / float(BIT_PRECISION * 8));

	delete[] data;
	return 0;
}