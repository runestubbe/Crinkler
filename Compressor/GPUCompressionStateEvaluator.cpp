#include "GPUCompressionStateEvaluator.h"

GPUCompressionStateEvaluator::GPUCompressionStateEvaluator() {

}

GPUCompressionStateEvaluator::~GPUCompressionStateEvaluator() {

}

bool GPUCompressionStateEvaluator::init(ModelPredictions* models, int length, int baseprobs[8]) {
	return true;
}

long long GPUCompressionStateEvaluator::evaluate(const ModelList& models) {
	return 0;
}
