
section	.data

%macro INCBIN 2
%defstr T PATH
%strcat S T, %2
global %1
global _ %+ %1
global %1 %+ _end
global _ %+ %1 %+ _end
%1:
_ %+ %1:
incbin S
%1 %+ _end:
_ %+ %1 %+ _end:
%endmacro

INCBIN headerObj, "header30.obj"
INCBIN headerCompatibilityObj, "header20_compatibility.obj"
INCBIN header1KObj, "header22_1k.obj"

INCBIN importObj, "import20.obj"
INCBIN importRangeObj, "import20-range.obj"
INCBIN importSafeObj, "import20-safe.obj"
INCBIN importSafeRangeObj, "import20-safe-range.obj"
INCBIN importSafeFallbackObj, "import20-safe-fallback.obj"
INCBIN importSafeFallbackRangeObj, "import20-safe-fallback-range.obj"
INCBIN import1KObj, "import20_1k.obj"

INCBIN calltransObj, "calltrans.obj"
INCBIN runtimeObj, "runtime.obj"

global knownDllExports
global _knownDllExports
global knownDllExports_end
global _knownDllExports_end
knownDllExports:
_knownDllExports:
	incbin "known_dll_exports.dat"
knownDllExports_end:
_knownDllExports_end: