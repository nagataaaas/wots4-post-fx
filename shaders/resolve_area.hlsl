sampler2D scene : register(s0);    // The enlarged scene, point-sampled
float4 dimensions : register(c8);  // Inverse width/height, width/height of the scene.
float4 resolveScale : register(c14);  // xy: scene texels per output pixel (about 1.5).

// The share of scene texel [first, first + 1) inside the footprint [start, end).
float overlap(float first, float start, float end) {
    return saturate(min(first + 1, end) - max(first, start));
}

// Averages the scene texels under one output pixel, each weighted by the area it covers.
// A footprint up to 2 texels wide touches at most 3 texels per axis.
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 center = uv * dimensions.zw;
    float2 start = center - resolveScale.xy * 0.5, end = center + resolveScale.xy * 0.5;
    float2 first = floor(start);
    float4 sum = 0;
    float weights = 0;
    [unroll] for (int row = 0; row < 3; ++row) {
        float rowWeight = overlap(first.y + row, start.y, end.y);
        [unroll] for (int column = 0; column < 3; ++column) {
            float weight = rowWeight * overlap(first.x + column, start.x, end.x);
            float2 texel = (first + float2(column, row) + 0.5) * dimensions.xy;
            sum += tex2Dlod(scene, float4(texel, 0, 0)) * weight;
            weights += weight;
        }
    }
    return sum / max(weights, 0.000001);
}
