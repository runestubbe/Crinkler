sampler2D models[NUM_MODELS] : register(s0);
float weights[NUM_MODELS] : register(c0);

float4 ps_main(float2 texcoord:TEXCOORD0):COLOR {
	float2 res = 0;
	for(int i = 0; i < NUM_MODELS; i++)
		res += tex2D(models[i], texcoord).rg*weights[i];
		
#if DO_LOGS
	res.r = log2(res.r+res.g) - log2(res.r);
	return res.rrrr;
#else
	return res.rggg;
#endif
}