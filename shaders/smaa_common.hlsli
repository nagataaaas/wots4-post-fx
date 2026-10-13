float4 dimensions : register(c8);
#define SMAA_RT_METRICS dimensions
#define SMAA_HLSL_3
#define SMAA_PRESET_HIGH
#include "../third_party/smaa/SMAA.hlsl"
