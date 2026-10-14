float4 dimensions : register(c8);
#define SMAA_RT_METRICS dimensions
#define SMAA_HLSL_3
#define SMAA_PRESET_ULTRA

// Predication lowers the color threshold where the scene depth changes, so silhouettes
// against a similar color are found. The base threshold stays at the Ultra value.
#define SMAA_PREDICATION 1
#define SMAA_PREDICATION_SCALE 1.0
#define SMAA_PREDICATION_STRENGTH 0.5
// The game stores depth as 0.5 - 0.5 * z/w, which is close to proportional to 1/distance.
// Differences of its log2 are relative distance differences: 0.07 is about 5%.
#define SMAA_PREDICATION_THRESHOLD 0.07
// Sky and cleared pixels hold 0; they still differ from any surface by a large ratio.
static const float smallestEncodedDepth = 0.000001;

float logDepth(sampler2D depth, float2 uv) {
    return log2(max(tex2Dlod(depth, float4(uv, 0, 0)).r, smallestEncodedDepth));
}
// SMAA reads predication through SMAAGather when it is defined. topLeft is half a texel
// up and left of the pixel; the result's .grb must be the pixel, its left and top neighbors.
float4 predicationGather(sampler2D depth, float2 topLeft) {
    float2 center = topLeft + dimensions.xy * 0.5;
    return float4(logDepth(depth, center - float2(dimensions.x, 0)), logDepth(depth, center),
        logDepth(depth, center - float2(0, dimensions.y)), 0);
}
#define SMAAGather(tex, coord) predicationGather(tex, coord)
#include "../third_party/smaa/SMAA.hlsl"
