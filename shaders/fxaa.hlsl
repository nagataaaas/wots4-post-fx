sampler2D scene : register(s0);
float4 dimensions : register(c8); // Inverse width/height, width/height.

float3 colorAt(float2 uv) { return tex2Dlod(scene, float4(uv, 0, 0)).rgb; }
float luminance(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }

// Classify edges with cardinal neighbors, search their endpoints, then apply a
// perpendicular offset and a bounded subpixel filter. Center alpha is preserved.
float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    const float edgeThreshold = 0.0625, minimumThreshold = 0.0078125;
    const float subpixelTrim = 0.125, maximumSubpixelBlend = 0.75;
    float2 texel = dimensions.xy;
    float4 center = tex2Dlod(scene, float4(uv, 0, 0));
    float3 north = colorAt(uv + float2(0, -texel.y));
    float3 south = colorAt(uv + float2(0, texel.y));
    float3 west = colorAt(uv + float2(-texel.x, 0));
    float3 east = colorAt(uv + float2(texel.x, 0));
    float middle = luminance(center.rgb);
    float n = luminance(north), s = luminance(south), w = luminance(west), e = luminance(east);
    float minimum = min(middle, min(min(n, s), min(w, e)));
    float maximum = max(middle, max(max(n, s), max(w, e)));
    float range = maximum - minimum;
    if (range < max(minimumThreshold, maximum * edgeThreshold)) return center;
    float3 northwest = colorAt(uv + float2(-1, -1) * texel);
    float3 northeast = colorAt(uv + float2(1, -1) * texel);
    float3 southwest = colorAt(uv + float2(-1, 1) * texel);
    float3 southeast = colorAt(uv + float2(1, 1) * texel);
    float nw = luminance(northwest), ne = luminance(northeast);
    float sw = luminance(southwest), se = luminance(southeast);
    float horizontal = abs(nw + sw - 2 * w) + 2 * abs(n + s - 2 * middle) + abs(ne + se - 2 * e);
    float vertical = abs(nw + ne - 2 * n) + 2 * abs(w + e - 2 * middle) + abs(sw + se - 2 * s);
    bool horizontalEdge = horizontal >= vertical;
    float negative = horizontalEdge ? n : w, positive = horizontalEdge ? s : e;
    bool useNegative = abs(negative - middle) >= abs(positive - middle);
    float neighbor = useNegative ? negative : positive;
    float gradient = abs(neighbor - middle);
    float pairAverage = 0.5 * (middle + neighbor);
    float2 perpendicular = horizontalEdge ? float2(0, texel.y) : float2(texel.x, 0);
    perpendicular *= useNegative ? -1 : 1;
    float2 along = horizontalEdge ? float2(texel.x, 0) : float2(0, texel.y);
    float2 edgeCenter = uv + 0.5 * perpendicular;
    const float searchDistances[9] = {1, 1.5, 2, 3, 4, 6, 8, 12, 16};
    float negativeDistance = 16, positiveDistance = 16;
    float negativeDelta = 0, positiveDelta = 0;
    bool negativeFound = false, positiveFound = false;
    [unroll] for (int step = 0; step < 9; ++step) {
        if (!negativeFound) {
            negativeDelta = luminance(colorAt(edgeCenter - along * searchDistances[step])) - pairAverage;
            negativeDistance = searchDistances[step];
            negativeFound = abs(negativeDelta) >= gradient * 0.25;
        }
        if (!positiveFound) {
            positiveDelta = luminance(colorAt(edgeCenter + along * searchDistances[step])) - pairAverage;
            positiveDistance = searchDistances[step];
            positiveFound = abs(positiveDelta) >= gradient * 0.25;
        }
    }
    bool closestNegative = negativeDistance <= positiveDistance;
    float endpointDelta = closestNegative ? negativeDelta : positiveDelta;
    bool endpointFound = closestNegative ? negativeFound : positiveFound;
    float offset = 0;
    if (endpointFound && (endpointDelta < 0) != (middle - pairAverage < 0))
        offset = 0.5 - min(negativeDistance, positiveDistance) / (negativeDistance + positiveDistance);
    float3 edgeColor = colorAt(uv + perpendicular * offset);
    float neighborhood = 0.25 * (n + s + w + e);
    float subpixel = min(maximumSubpixelBlend, saturate((abs(neighborhood - middle) / range - subpixelTrim) /
        (1 - subpixelTrim)));
    float3 lowpass = (center.rgb + north + south + west + east + northwest + northeast + southwest + southeast) / 9;
    return float4(lerp(edgeColor, lowpass, subpixel), center.a);
}
