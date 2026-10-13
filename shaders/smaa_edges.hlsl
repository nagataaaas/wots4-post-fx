#include "smaa_common.hlsli"
sampler2D colorTexture : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 offsets[3];
    SMAAEdgeDetectionVS(uv, offsets);
    return float4(SMAAColorEdgeDetectionPS(uv, offsets, colorTexture), 0, 0);
}
