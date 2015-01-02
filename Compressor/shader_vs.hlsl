float2 pixelOffset : register(c0);

struct VS_OUTPUT {
	float4 Position:POSITION0;
	float2 Texcoord:TEXCOORD0;
};

VS_OUTPUT vs_main(float4 pos:POSITION) {
   VS_OUTPUT O;
   O.Position = pos;
   O.Texcoord = (pos + 1.0f)*0.5f;
   O.Texcoord.y *= -1;
   return O;
}