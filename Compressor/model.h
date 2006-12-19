extern "C" {
	typedef unsigned char byte;
	inline int __stdcall GetBit(const byte *data, int bitpos) {
		return (data[bitpos >> 3] >> (7 - bitpos & 7)) & 1;
	}
	unsigned int __stdcall ModelHash(const byte *data, int bitpos, unsigned int mask);
};
