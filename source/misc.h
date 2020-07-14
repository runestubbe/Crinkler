#pragma once
#ifndef _MISC_H_
#define _MISC_H_

int align(int v, int alignmentBits);

// Round integer to given precision
unsigned long long roundInt64(unsigned long long v, int bits);

int numberOfModelsInWeightMask(unsigned int mask);

#endif
