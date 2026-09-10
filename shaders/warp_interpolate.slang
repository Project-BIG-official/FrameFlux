// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: Particle-Aware Warping with Rain Ghosting Rejection

enum FallbackAction : uint32_t {
    RepeatFrame = 0,
    BlendFrame  = 1,
    DropFrame   = 2
};

struct PushConstants {
    float t;
    float confidenceThreshold;
    uint32_t fallbackAction;
    uint32_t showDebugWatermark;
    uint2 resolution;
    float2 invResolution;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<float4> frameA;
[[vk::binding(1, 0)]] Texture2D<float4> frameB;
[[vk::binding(2, 0)]] Texture2D<float2> motionVectors;
[[vk::binding(3, 0)]] Texture2D<float>  confidenceMap;
[[vk::binding(4, 0)]] SamplerState      linearSampler;

[[vk::binding(5, 0)]] RWTexture2D<float4> outputFrame;

float4 sampleColor(Texture2D<float4> tex, float2 uv) {
    float2 clampedUV = clamp(uv, float2(0.0001), float2(0.9999));
    return tex.SampleLevel(linearSampler, clampedUV, 0.0);
}

// 5-tap median filter to eliminate rogue motion vectors produced by rain/particles
float2 getFilteredMotion(float2 uv) {
    float2 offsets[5] = {
        float2(0.0, 0.0),
        float2(-1.5,  0.0) * pc.invResolution,
        float2( 1.5,  0.0) * pc.invResolution,
        float2( 0.0, -1.5) * pc.invResolution,
        float2( 0.0,  1.5) * pc.invResolution
    };

    float2 v0 = motionVectors.SampleLevel(linearSampler, uv + offsets[0], 0.0);
    float2 v1 = motionVectors.SampleLevel(linearSampler, uv + offsets[1], 0.0);
    float2 v2 = motionVectors.SampleLevel(linearSampler, uv + offsets[2], 0.0);
    float2 v3 = motionVectors.SampleLevel(linearSampler, uv + offsets[3], 0.0);
    float2 v4 = motionVectors.SampleLevel(linearSampler, uv + offsets[4], 0.0);

    // Approximate component-wise median
    float2 min1 = min(v0, v1); float2 max1 = max(v0, v1);
    float2 min2 = min(v2, v3); float2 max2 = max(v2, v3);
    float2 midMin = max(min1, min2);
    float2 midMax = min(max1, max2);
    return clamp(v4, min(midMin, midMax), max(midMin, midMax));
}

// Computes 3x3 neighborhood bounding box to prevent ghost halos
void getNeighborhoodBox(uint2 pixelCoord, out float3 boxMin, out float3 boxMax) {
    int2 center = int2(pixelCoord);
    boxMin = float3(1e5);
    boxMax = float3(-1e5);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 sampleCoord = clamp(center + int2(x, y), int2(0), int2(pc.resolution) - 1);
            float3 c = frameB[sampleCoord].rgb;
            boxMin = min(boxMin, c);
            boxMax = max(boxMax, c);
        }
    }
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint32_t3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) return;

    float4 rawA = frameA[pixelCoord];
    float4 rawB = frameB[pixelCoord];

    // =========================================================================
    // 1. STATIC UI / HUD PRESERVATION (Crosshair, Minimap, Text)
    // =========================================================================
    float rawColorDiff = max(abs(rawA.r - rawB.r), max(abs(rawA.g - rawB.g), abs(rawA.b - rawB.b)));
    if (rawColorDiff < 0.032) {
        outputFrame[pixelCoord] = rawB;
        return;
    }

    // =========================================================================
    // 2. TRANSIENT PARTICLE & RAIN DETECTION (High-Frequency Luma Laplacian)
    // =========================================================================
    // Raindrops and sparks are thin (1-2px) local spatial spikes present in one
    // frame, but absent in the other. We detect spatial cross-variance.
    int2 coord = int2(pixelCoord);
    int2 clampMax = int2(pc.resolution) - 1;

    float4 nbL = frameB[clamp(coord + int2(-1,  0), int2(0), clampMax)];
    float4 nbR = frameB[clamp(coord + int2( 1,  0), int2(0), clampMax)];
    float4 nbU = frameB[clamp(coord + int2( 0, -1), int2(0), clampMax)];
    float4 nbD = frameB[clamp(coord + int2( 0,  1), int2(0), clampMax)];
    float3 avgBNeighbors = (nbL.rgb + nbR.rgb + nbU.rgb + nbD.rgb) * 0.25;

    float4 naL = frameA[clamp(coord + int2(-1,  0), int2(0), clampMax)];
    float4 naR = frameA[clamp(coord + int2( 1,  0), int2(0), clampMax)];
    float4 naU = frameA[clamp(coord + int2( 0, -1), int2(0), clampMax)];
    float4 naD = frameA[clamp(coord + int2( 0,  1), int2(0), clampMax)];
    float3 avgANeighbors = (naL.rgb + naR.rgb + naU.rgb + naD.rgb) * 0.25;

    float localContrastB = length(rawB.rgb - avgBNeighbors);
    float localContrastA = length(rawA.rgb - avgANeighbors);

    // Rain drop in Frame B: thin high-frequency contrast in B, but absent in A
    bool isRainParticleB = (localContrastB > 0.075 && localContrastA < 0.025);
    // Rain drop in Frame A: thin high-frequency contrast in A, but gone in B
    bool isRainParticleA = (localContrastA > 0.075 && localContrastB < 0.025);

    if (isRainParticleB) {
        // Do NOT smear Frame B's raindrops with background vectors!
        // Introduce smoothly based on time factor without trailing smears
        outputFrame[pixelCoord] = (pc.t < 0.35) ? rawA : lerp(rawA, rawB, smoothstep(0.35, 1.0, pc.t));
        return;
    }

    if (isRainParticleA) {
        // Fade out Frame A's disappearing raindrop cleanly
        outputFrame[pixelCoord] = (pc.t > 0.65) ? rawB : lerp(rawA, rawB, smoothstep(0.0, 0.65, pc.t));
        return;
    }

    // =========================================================================
    // 3. MOTION-COMPENSATED WARPING WITH MEDIAN FILTERING
    // =========================================================================
    float2 uv = (float2(pixelCoord) + 0.5) * pc.invResolution;

    // Use median-filtered vector to reject isolated rain-induced vector anomalies
    float2 motion = getFilteredMotion(uv);
    float confidence = confidenceMap.SampleLevel(linearSampler, uv, 0.0);

    // Temporal fallback on occlusions/unreliable vectors
    if (confidence < pc.confidenceThreshold) {
        outputFrame[pixelCoord] = (pc.t < 0.5) ? rawA : rawB;
        return;
    }

    // Warped lookups
    float2 motionUV = motion * pc.invResolution;
    float2 uvA = uv - (motionUV * pc.t);
    float2 uvB = uv + (motionUV * (1.0 - pc.t));

    float4 sampleA = sampleColor(frameA, uvA);
    float4 sampleB = sampleColor(frameB, uvB);

    // =========================================================================
    // 4. DISOCCLUSION CHECK & COLOR BOX CLAMPING (Anti-Halo)
    // =========================================================================
    float lumaA = dot(sampleA.rgb, float3(0.299, 0.587, 0.114));
    float lumaB = dot(sampleB.rgb, float3(0.299, 0.587, 0.114));
    float photometricDiff = abs(lumaA - lumaB);

    float4 finalColor;
    if (photometricDiff > 0.20) {
        // Disocclusion: sample closest real temporal frame, avoiding semi-transparent silhouettes
        finalColor = (pc.t < 0.5) ? sampleA : sampleB;
    } else {
        float4 interpolated = lerp(sampleA, sampleB, pc.t);

        // Clamp interpolated color to the 3x3 neighborhood of Frame B
        // This stops color bleeding and trailing halos around high-contrast edges
        float3 boxMin, boxMax;
        getNeighborhoodBox(pixelCoord, boxMin, boxMax);
        float3 clampedRgb = clamp(interpolated.rgb, boxMin - float3(0.03), boxMax + float3(0.03));

        finalColor = float4(clampedRgb, interpolated.a);
    }

    outputFrame[pixelCoord] = finalColor;
}