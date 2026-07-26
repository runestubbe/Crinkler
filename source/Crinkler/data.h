#pragma once
#ifndef _DATA_H_
#define _DATA_H_

extern "C" {
	extern char headerObj[];
	extern char headerObj_end[];

	extern char headerCompatibilityObj[];
	extern char headerCompatibilityObj_end[];

	extern char header1KObj[];
	extern char header1KObj_end[];

	extern char import1KObj[];
	extern char import1KObj_end[];

	extern char importObj[];
	extern char importObj_end[];

	extern char importRangeObj[];
	extern char importRangeObj_end[];

	extern char calltransObj[];
	extern char calltransObj_end[];

	extern char runtimeObj[];
	extern char runtimeObj_end[];

	extern char knownDllExports[];
	extern char knownDllExports_end[];

	extern char iconImage[];
	extern char iconImage_end[];
};

#endif