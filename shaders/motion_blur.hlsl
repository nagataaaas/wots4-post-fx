sampler2D scene : register(s0);
sampler2D depthTexture : register(s1);
row_major float4x4 inverseCurrent : register(c0);
row_major float4x4 previous : register(c4);
float4 dimensions : register(c8);
float4 exposure : register(c9); // Exposure scale, maximum pixels, samples, relative depth tolerance.
float4 cameraOrigin : register(c10);
float4 nearBlur : register(c11); // Fade start/end distance, minimum exposure ratio, near width limit.
float4 cameraForward : register(c12);

bool worldPosition(float2 uv, float encodedDepth, out float4 world) {
    world = mul(float4(2 * uv.x - 1, 1 - 2 * uv.y, 1 - 2 * encodedDepth, 1), inverseCurrent);
    if (abs(world.w) < 0.000001) return false;
    world /= world.w;
    return true;
}

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float4 center = tex2D(scene, uv);
    float encodedDepth = tex2D(depthTexture, uv).r;
    float4 world;
    if (encodedDepth <= 0.000001 || encodedDepth > 0.5001 || !worldPosition(uv, encodedDepth, world)) return center;
    float4 oldClip = mul(world, previous);
    if (oldClip.w <= 0.000001) return center;
    float2 oldUV = float2(oldClip.x / oldClip.w * 0.5 + 0.5, 0.5 - oldClip.y / oldClip.w * 0.5);
    float centerDistance = length(world.xyz - cameraOrigin.xyz);
    float centerViewDepth = dot(world.xyz - cameraOrigin.xyz, cameraForward.xyz);
    float proximity = smoothstep(nearBlur.x, nearBlur.y, centerDistance);
    float exposureScale = exposure.x * lerp(nearBlur.z, 1, proximity);
    float widthLimit = lerp(nearBlur.w, exposure.y, proximity);
    float2 velocity = (oldUV - uv) * exposureScale;
    float pixels = length(velocity * dimensions.zw);
    velocity *= min(1, widthLimit / max(pixels, 0.000001));
    float3 sum = center.rgb;
    float weights = 1;
    // Reconstruct distance in world units; projected depth differences are nonlinear.
    [loop] for (int sample = 1; sample < (int)exposure.z; ++sample) {
        float2 sampleUV = uv + velocity * ((float)sample / (exposure.z - 1));
        if (any(sampleUV < dimensions.xy * 0.5) || any(sampleUV > 1 - dimensions.xy * 0.5)) continue;
        float sampleDepth = tex2Dlod(depthTexture, float4(sampleUV, 0, 0)).r;
        float4 sampleWorld;
        if (sampleDepth <= 0.000001 || sampleDepth > 0.5001 || !worldPosition(sampleUV, sampleDepth, sampleWorld)) continue;
        // Neighboring points on one surface differ in XY, which is not a depth edge.
        float sampleViewDepth = dot(sampleWorld.xyz - cameraOrigin.xyz, cameraForward.xyz);
        if (abs(sampleViewDepth - centerViewDepth) > max(0.25, centerViewDepth * exposure.w)) continue;
        sum += tex2Dlod(scene, float4(sampleUV, 0, 0)).rgb;
        weights += 1;
    }
    return float4(sum / weights, center.a);
}
