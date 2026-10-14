#include "smaa_common.hlsli"
sampler2D colorTexture : register(s0);
sampler2D depthTexture : register(s1);  // Unbound when no matching depth exists; it then reads 0 and predicts nothing
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 offsets[3];
    SMAAEdgeDetectionVS(uv, offsets);
    return float4(SMAAColorEdgeDetectionPS(uv, offsets, colorTexture, depthTexture), 0, 0);
}
