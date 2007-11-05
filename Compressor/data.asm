global _vertexshaderSource
global _pixelshaderSource
global _downscaleSource

section	shad data
_vertexshaderSource:
incbin "shader_vs.hlsl"
db 0

_pixelshaderSource:
incbin "shader_ps.hlsl"
db 0

_downscaleSource:
incbin "downscale_ps.hlsl"
db 0