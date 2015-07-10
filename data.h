#pragma once
#ifndef _DATA_H_
#define _DATA_H_

extern "C" {
	char headerObj[];
	char headerObj_end[];

	char header11Obj[];
	char header11Obj_end[];

	char header14CompatibilityObj[];
	char header14CompatibilityObj_end[];

	char header1KObj[];
	char header1KObj_end[];

	char import1KObj[];
	char import1KObj_end[];

	char importObj[];
	char importObj_end[];

	char importRangeObj[];
	char importRangeObj_end[];
	
	char importSafeObj[];
	char importSafeObj_end[];

	char importSafeRangeObj[];
	char importSafeRangeObj_end[];

	char importSafeFallbackObj[];
	char importSafeFallbackObj_end[];

	char importSafeFallbackRangeObj[];
	char importSafeFallbackRangeObj_end[];

	char calltransObj[];
	char calltransObj_end[];
};

#endif