#include "smaa_common.hlsli"
sampler2D edgesTexture : register(s0);
sampler2D areaTexture : register(s1);
sampler2D searchTexture : register(s2);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 pixel;
    float4 offsets[3];
    SMAABlendingWeightCalculationVS(uv, pixel, offsets);
    return SMAABlendingWeightCalculationPS(uv, pixel, offsets, edgesTexture, areaTexture, searchTexture, 0);
}
