// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: Particle-Aware Warping with Live Optical Flow & Confidence Visualizers

enum FallbackAction : uint {
    RepeatFrame = 0,
    BlendFrame  = 1,
    DropFrame   = 2
};

struct PushConstants {
    float t;
    float confidenceThreshold;
    uint fallbackAction;
    uint showDebugWatermark;
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

    float2 min1 = min(v0, v1); float2 max1 = max(v0, v1);
    float2 min2 = min(v2, v3); float2 max2 = max(v2, v3);
    float2 midMin = max(min1, min2);
    float2 midMax = min(max1, max2);
    return clamp(v4, min(midMin, midMax), max(midMin, midMax));
}

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

float3 flowToColorWheel(float2 flow) {
    float len = length(flow);
    if (len < 0.25) return float3(0.04, 0.04, 0.06);

    float angle = atan2(flow.y, flow.x);
    float hue = (angle / 6.2831853) + 0.5;

    float3 rgb = saturate(abs(frac(hue + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    float intensity = saturate(len / 20.0);
    return lerp(float3(0.1, 0.1, 0.12), rgb, intensity);
}

float3 confidenceToHeatmap(float conf, float3 bgRgb) {
    float3 heatColor;
    if (conf > 0.5) {
        heatColor = lerp(float3(1.0, 0.85, 0.0), float3(0.0, 1.0, 0.25), (conf - 0.5) * 2.0);
    } else {
        heatColor = lerp(float3(0.95, 0.05, 0.15), float3(1.0, 0.85, 0.0), conf * 2.0);
    }
    float mono = dot(bgRgb, float3(0.299, 0.587, 0.114));
    return lerp(float3(mono * 0.25), heatColor, 0.85);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) return;

    float2 uv = (float2(pixelCoord) + 0.5) * pc.invResolution;
    float4 rawA = frameA[pixelCoord];
    float4 rawB = frameB[pixelCoord];

    // =========================================================================
    // 0. LIVE DEBUG VISUALIZERS (FLOW VECTORS & CONFIDENCE HEATMAP)
    // =========================================================================
    if (pc.showDebugWatermark == 3) {
        float2 flow = getFilteredMotion(uv);
        outputFrame[pixelCoord] = float4(flowToColorWheel(flow), 1.0);
        return;
    }

    if (pc.showDebugWatermark == 2) {
        float conf = confidenceMap.SampleLevel(linearSampler, uv, 0.0);
        outputFrame[pixelCoord] = float4(confidenceToHeatmap(conf, rawB.rgb), 1.0);
        return;
    }

    // =========================================================================
    // 1. STATIC UI / HUD PRESERVATION
    // =========================================================================
    float rawColorDiff = max(abs(rawA.r - rawB.r), max(abs(rawA.g - rawB.g), abs(rawA.b - rawB.b)));
    if (rawColorDiff < 0.032) {
        outputFrame[pixelCoord] = rawB;
        return;
    }

    // =========================================================================
    // 2. TRANSIENT PARTICLE DETECTION
    // =========================================================================
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

    bool isRainParticleB = (localContrastB > 0.075 && localContrastA < 0.025);
    bool isRainParticleA = (localContrastA > 0.075 && localContrastB < 0.025);

    if (isRainParticleB) {
        outputFrame[pixelCoord] = (pc.t < 0.35) ? rawA : lerp(rawA, rawB, smoothstep(0.35, 1.0, pc.t));
        return;
    }

    if (isRainParticleA) {
        outputFrame[pixelCoord] = (pc.t > 0.65) ? rawB : lerp(rawA, rawB, smoothstep(0.0, 0.65, pc.t));
        return;
    }

    // =========================================================================
    // 3. MOTION-COMPENSATED WARPING
    // =========================================================================
    float2 motion = getFilteredMotion(uv);
    float confidence = confidenceMap.SampleLevel(linearSampler, uv, 0.0);

    if (confidence < pc.confidenceThreshold) {
        if (pc.fallbackAction == FallbackAction::BlendFrame) {
            outputFrame[pixelCoord] = lerp(rawA, rawB, 0.5);
        } else {
            outputFrame[pixelCoord] = (pc.t < 0.5) ? rawA : rawB;
        }
        return;
    }

    float2 motionUV = motion * pc.invResolution;
    float2 uvA = uv - (motionUV * pc.t);
    float2 uvB = uv + (motionUV * (1.0 - pc.t));

    float4 sampleA = sampleColor(frameA, uvA);
    float4 sampleB = sampleColor(frameB, uvB);

    // =========================================================================
    // 4. DISOCCLUSION CHECK & COLOR BOX CLAMPING
    // =========================================================================
    float lumaA = dot(sampleA.rgb, float3(0.299, 0.587, 0.114));
    float lumaB = dot(sampleB.rgb, float3(0.299, 0.587, 0.114));
    float photometricDiff = abs(lumaA - lumaB);

    float4 finalColor;
    if (photometricDiff > 0.20) {
        finalColor = (pc.t < 0.5) ? sampleA : sampleB;
    } else {
        float4 interpolated = lerp(sampleA, sampleB, pc.t);

        float3 boxMin, boxMax;
        getNeighborhoodBox(pixelCoord, boxMin, boxMax);
        float3 clampedRgb = clamp(interpolated.rgb, boxMin - float3(0.03), boxMax + float3(0.03));

        finalColor = float4(clampedRgb, interpolated.a);
    }

    if (pc.showDebugWatermark == 1) {
        uint badgeX = pc.resolution.x - 56;
        uint badgeY = 16;
        if (pixelCoord.x >= badgeX && pixelCoord.x < badgeX + 40 &&
            pixelCoord.y >= badgeY && pixelCoord.y < badgeY + 18) 
        {
            uint bx = pixelCoord.x - badgeX;
            uint by = pixelCoord.y - badgeY;
            if (bx == 0 || bx == 39 || by == 0 || by == 17) {
                finalColor = float4(0.0, 1.0, 0.5, 1.0);
            } else {
                finalColor = float4(0.01, 0.18, 0.08, 0.95);
            }
        }
    }

    outputFrame[pixelCoord] = finalColor;
}