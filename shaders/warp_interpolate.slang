// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: Motion-Compensated Warping with Arbitrary Time Parameter (t)

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

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) return;

    float2 uv = (float2(pixelCoord) + 0.5) * pc.invResolution;

    float2 motion = motionVectors.SampleLevel(linearSampler, uv, 0.0);
    float confidence = confidenceMap.SampleLevel(linearSampler, uv, 0.0);

    float4 finalColor;

    if (confidence < pc.confidenceThreshold) {
        float4 colA = sampleWarped(frameA, uv);
        float4 colB = sampleWarped(frameB, uv);
        finalColor = lerp(colA, colB, pc.t);
    } else {
        float2 motionUV = motion * pc.invResolution;
        float2 uvA = uv - (motionUV * pc.t);
        float2 uvB = uv + (motionUV * (1.0 - pc.t));

        float4 sampleA = sampleWarped(frameA, uvA);
        float4 sampleB = sampleWarped(frameB, uvB);

        finalColor = lerp(sampleA, sampleB, pc.t);
    }

    // Only draw debug watermark when explicitly requested via FRAMEFLUX_DEBUG=1
    if (pc.showDebugWatermark != 0 && pixelCoord.x < 24 && pixelCoord.y < 24) {
        finalColor = float4(0.0, 1.0, 0.0, 1.0);
    }

    outputFrame[pixelCoord] = finalColor;
}