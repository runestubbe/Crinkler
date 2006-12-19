global _headerObj
global _headerObj_end
global _importObj
global _importObj_end
global _importSafeObj
global _importSafeObj_end
global _calltransObj
global _calltransObj_end

section	.data

_headerObj:
	incbin "modules/header10.obj"
_headerObj_end:

_importObj:
	incbin "modules/import10.obj"
_importObj_end:

_importSafeObj:
	incbin "modules/import10-safe.obj"
_importSafeObj_end:

_calltransObj:
	incbin "modules/calltrans.obj"
_calltransObj_end: