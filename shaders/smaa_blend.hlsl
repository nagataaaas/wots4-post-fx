#include "smaa_common.hlsli"
sampler2D colorTexture : register(s0);
sampler2D weightsTexture : register(s1);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 offsets;
    SMAANeighborhoodBlendingVS(uv, offsets);
    float4 blended = SMAANeighborhoodBlendingPS(uv, offsets, colorTexture, weightsTexture);
    // Alpha belongs to the game's later composition and must not spread with edges.
    return float4(blended.rgb, tex2D(colorTexture, uv).a);
}
