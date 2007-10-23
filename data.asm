global _headerObj
global _headerObj_end
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
global _calltransObj
global _calltransObj_end

section	.data

_headerObj:
	incbin "modules/header11.obj"
_headerObj_end:

_header1KObj:
	incbin "modules/header10_1k.obj"
_header1KObj_end:

_import1KObj:
	incbin "modules/import10_1k.obj"
_import1KObj_end:

_importObj:
	incbin "modules/import10.obj"
_importObj_end:

_importRangeObj:
	incbin "modules/import10-range.obj"
_importRangeObj_end:

_importSafeObj:
	incbin "modules/import10-safe.obj"
_importSafeObj_end:

_importSafeRangeObj:
	incbin "modules/import10-safe-range.obj"
_importSafeRangeObj_end:

_calltransObj:
	incbin "modules/calltrans.obj"
_calltransObj_end: