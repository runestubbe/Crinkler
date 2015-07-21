global _header11Obj
global _header11Obj_end
global _headerObj
global _headerObj_end
global _headerCompatibilityObj
global _headerCompatibilityObj_end
global _header1KObj
global _header1KObj_end
global _import1KObj
global _import1KObj_end
global _importObj
global _importObj_end
global _importRangeObj
global _importRangeObj_end
global _importSafeObj
global _importSafeObj_end
global _importSafeRangeObj
global _importSafeRangeObj_end
global _importSafeFallbackObj
global _importSafeFallbackObj_end
global _importSafeFallbackRangeObj
global _importSafeFallbackRangeObj_end
global _calltransObj
global _calltransObj_end

section	.data

_header11Obj:
	incbin "modules/header11.obj"
_header11Obj_end:

_headerObj:
	incbin "modules/header20.obj"
_headerObj_end:

_headerCompatibilityObj:
	incbin "modules/header20_compatibility.obj"
_headerCompatibilityObj_end:

_header1KObj:
	incbin "modules/header20_1k.obj"
_header1KObj_end:

_import1KObj:
	incbin "modules/import20_1k.obj"
_import1KObj_end:

_importObj:
	incbin "modules/import20.obj"
_importObj_end:

_importRangeObj:
	incbin "modules/import20-range.obj"
_importRangeObj_end:

_importSafeObj:
	incbin "modules/import20-safe.obj"
_importSafeObj_end:

_importSafeRangeObj:
	incbin "modules/import20-safe-range.obj"
_importSafeRangeObj_end:

_importSafeFallbackObj:
	incbin "modules/import20-safe-fallback.obj"
_importSafeFallbackObj_end:

_importSafeFallbackRangeObj:
	incbin "modules/import20-safe-fallback-range.obj"
_importSafeFallbackRangeObj_end:

_calltransObj:
	incbin "modules/calltrans.obj"
_calltransObj_end:
