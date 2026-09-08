// PB FrameFlux - LGPL-2.1
// fallback_blend.slang: High-Speed Interpolation & Action Fallbacks (Blend / Repeat / Drop)

enum FallbackAction : uint32_t {
    RepeatFrame = 0,
    BlendFrame  = 1,
    DropFrame   = 2
};

struct FallbackPushConstants {
    float t;
    uint32_t action;
    uint2 resolution;
    float2 invResolution;
};

[[vk::push_constant]]
ConstantBuffer<FallbackPushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<float4>   frameA;
[[vk::binding(1, 0)]] Texture2D<float4>   frameB;
[[vk::binding(2, 0)]] RWTexture2D<float4> outputFrame;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) return;

    float4 colA = frameA[pixelCoord];
    float4 colB = frameB[pixelCoord];
    float4 finalColor;

    switch (pc.action) {
        case (uint32_t)FallbackAction::RepeatFrame:
            // Repeat closest real frame in time
            finalColor = (pc.t < 0.5) ? colA : colB;
            break;

        case (uint32_t)FallbackAction::DropFrame:
            // Under Drop on VRR, immediately advance to target frame B
            finalColor = colB;
            break;

        case (uint32_t)FallbackAction::BlendFrame:
        default:
            finalColor = lerp(colA, colB, pc.t);
            break;
    }

    outputFrame[pixelCoord] = finalColor;
}