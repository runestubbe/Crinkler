sampler2D src : register(s0);
float2 pixeloffset : register(c0);

float4 ps_main(float2 texcoord:TEXCOORD0):COLOR {
	float res = 0;
	res += tex2D(src, texcoord).r;
	res += tex2D(src, texcoord+pixeloffset).r;
	return res.rrrr;
}