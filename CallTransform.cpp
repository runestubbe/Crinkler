#include "CallTransform.h"
#include "Hunk.h"
/*
int CallTransform::transform(Hunk* hunk, int splittingPoint, int max_num) {
	int num = 0;
	unsigned char* data = (unsigned char*) hunk->getPtr();
	int size = splittingPoint;
	for (int i = 0 ; i < size-4 ; i++) {
		if (data[i] == 0xe8) {
			int *offset = (int *)&data[i+1];
			if (*offset >= -32768 && *offset <= 32767) {
				*offset = (int)(short)(*offset + i+1);
				i += 4;
				num++;
				if (num == max_num) break;
			}
		}
	}
	return num;
}*/