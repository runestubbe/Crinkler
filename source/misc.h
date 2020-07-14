#pragma once
#ifndef _MISC_H_
#define _MISC_H_

int Align(int v, int alignmentBits);

// Round integer to given precision
unsigned long long RoundInt64(unsigned long long v, int bits);

int NumberOfModelsInWeightMask(unsigned int mask);

#endif
