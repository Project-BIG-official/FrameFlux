// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: Motion-Compensated Warping with Anti-Ghosting & UI Protection

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

float4 sampleWarped(Texture2D<float4> tex, float2 uv) {
    float2 clampedUV = clamp(uv, float2(0.0), float2(1.0));
    return tex.SampleLevel(linearSampler, clampedUV, 0.0);
}

// Bounding-box neighborhood clamping (safe against ghosting halos)
float4 clampToNeighborhood(float4 color, Texture2D<float4> texB, uint2 centerPixel) {
    int2 center = int2(centerPixel);
    float4 centerCol = texB[centerPixel];
    float4 minCol = centerCol;
    float4 maxCol = centerCol;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            int2 sampleCoord = clamp(center + int2(dx, dy), int2(0), int2(pc.resolution) - 1);
            float4 c = texB[sampleCoord];
            minCol = min(minCol, c);
            maxCol = max(maxCol, c);
        }
    }
    return clamp(color, minCol, maxCol);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint32_t3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) return;

    float2 uv = (float2(pixelCoord) + 0.5) * pc.invResolution;

    float2 motion = motionVectors.SampleLevel(linearSampler, uv, 0.0);
    float confidence = confidenceMap.SampleLevel(linearSampler, uv, 0.0);

    // 1. Static UI & crosshair lock (Zero-Motion Protection)
    if (dot(motion, motion) < 0.02 && confidence > 0.85) {
        outputFrame[pixelCoord] = frameB[pixelCoord];
        return;
    }

    float4 finalColor;

    // 2. Low confidence fallback
    if (confidence < pc.confidenceThreshold) {
        float4 colA = sampleWarped(frameA, uv);
        float4 colB = sampleWarped(frameB, uv);

        switch (pc.fallbackAction) {
            case (uint32_t)FallbackAction::RepeatFrame:
                finalColor = (pc.t < 0.5) ? colA : colB;
                break;

            case (uint32_t)FallbackAction::DropFrame:
                finalColor = colB;
                break;

            case (uint32_t)FallbackAction::BlendFrame:
            default:
                finalColor = lerp(colA, colB, pc.t);
                break;
        }
    } else {
        // 3. Motion compensation with photometric disocclusion
        float2 motionUV = motion * pc.invResolution;
        float2 uvA = uv - (motionUV * pc.t);
        float2 uvB = uv + (motionUV * (1.0 - pc.t));

        float4 sampleA = sampleWarped(frameA, uvA);
        float4 sampleB = sampleWarped(frameB, uvB);

        float lumaA = dot(sampleA.rgb, float3(0.299, 0.587, 0.114));
        float lumaB = dot(sampleB.rgb, float3(0.299, 0.587, 0.114));
        float colorDiff = abs(lumaA - lumaB);

        if (colorDiff > 0.35) {
            float edgeWeight = smoothstep(0.1, 0.9, pc.t);
            float4 rawInterpolated = lerp(sampleA, sampleB, edgeWeight);
            finalColor = clampToNeighborhood(rawInterpolated, frameB, pixelCoord);
        } else {
            finalColor = lerp(sampleA, sampleB, pc.t);
        }
    }

    outputFrame[pixelCoord] = finalColor;
}