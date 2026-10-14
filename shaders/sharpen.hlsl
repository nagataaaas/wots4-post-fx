// Contrast adaptive sharpening, ported to ps_3_0 from AMD FidelityFX CAS 1.20190610
// (sharpen-only path with CAS_BETTER_DIAGONALS and exact reciprocals).
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. MIT license: docs/licenses/cas.txt
sampler2D scene : register(s0);
float4 dimensions : register(c8);  // Inverse width/height, width/height.
float4 sharpening : register(c13);  // x: negative peak weight from the strength, -1/lerp(8, 5, strength).

float3 colorAt(float2 uv) { return tex2Dlod(scene, float4(uv, 0, 0)).rgb; }
float3 min3(float3 a, float3 b, float3 c) { return min(min(a, b), c); }
float3 max3(float3 a, float3 b, float3 c) { return max(max(a, b), c); }

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    // a b c
    // d e f
    // g h i
    float2 texel = dimensions.xy;
    float4 center = tex2Dlod(scene, float4(uv, 0, 0));
    float3 a = colorAt(uv + float2(-1, -1) * texel), b = colorAt(uv + float2(0, -1) * texel);
    float3 c = colorAt(uv + float2(1, -1) * texel), d = colorAt(uv + float2(-1, 0) * texel);
    float3 e = center.rgb, f = colorAt(uv + float2(1, 0) * texel);
    float3 g = colorAt(uv + float2(-1, 1) * texel), h = colorAt(uv + float2(0, 1) * texel);
    float3 i = colorAt(uv + float2(1, 1) * texel);
    // Soft minimum and maximum of the cross and the full 3x3, both doubled.
    float3 crossMinimum = min3(min3(d, e, f), b, h), crossMaximum = max3(max3(d, e, f), b, h);
    float3 minimum = crossMinimum + min3(min3(crossMinimum, a, c), g, i);
    float3 maximum = crossMaximum + max3(max3(crossMaximum, a, c), g, i);
    // Less sharpening where the neighborhood is already near black or white, which limits ringing.
    // Values above 1 (float targets) give no headroom and pass through unsharpened.
    float3 amount = sqrt(saturate(min(minimum, 2 - maximum) / max(maximum, 0.000001)));
    // CAS weights all channels with green to keep hues.
    float weight = amount.g * sharpening.x;
    float3 sharpened = ((b + d + f + h) * weight + e) / (1 + 4 * weight);
    return float4(max(sharpened, 0), center.a);
}
