// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: Bidirectional Warping & Dynamic Pacing

// Artifact protection modes
enum FallbackAction : uint32_t {
    RepeatFrame = 0,
    BlendFrame  = 1,
    DropFrame   = 2
};

// Frame parameters passed via Vulkan Push Constants
struct PushConstants {
    float t;                     // Normalized interpolation time (0.0 .. 1.0)
    float confidenceThreshold;   // Confidence drop threshold (e.g., 0.65)
    uint32_t fallbackAction;     // FallbackAction
    uint32_t searchMode;         // Standard / High
    uint2 resolution;            // Target frame resolution
    float2 invResolution;        // 1.0 / resolution
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

// Input resources
[[vk::binding(0, 0)]] Texture2D<float4> frameA;
[[vk::binding(1, 0)]] Texture2D<float4> frameB;
[[vk::binding(2, 0)]] Texture2D<float2> motionVectors; // Vectors A -> B in pixels
[[vk::binding(3, 0)]] Texture2D<float>  confidenceMap; // 0.0 (noise/breakdown) .. 1.0 (ideal)
[[vk::binding(4, 0)]] SamplerState      linearSampler;

// Output buffer for presentation
[[vk::binding(5, 0)]] RWTexture2D<float4> outputFrame;

// Bilinear color sampling with vector offset
float4 sampleWarped(Texture2D<float4> tex, float2 uv) {
    // Clamping coordinates to stay within screen bounds
    float2 clampedUV = clamp(uv, float2(0.0), float2(1.0));
    return tex.SampleLevel(linearSampler, clampedUV, 0.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;

    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) {
        return;
    }

    float2 uv = (float2(pixelCoord) + 0.5) * pc.invResolution;

    // 1. Read motion vector and reliability estimate
    float2 motion = motionVectors[pixelCoord];
    float confidence = confidenceMap[pixelCoord];

    float4 finalColor;

    // 2. Processing low-confidence zones (sharp turns, UI, disocclusions)
    if (confidence < pc.confidenceThreshold) {
        switch (pc.fallbackAction) {
            case (uint32_t)FallbackAction::RepeatFrame:
                // Duplicate the nearest real frame at time t
                finalColor = (pc.t < 0.5) ? frameA[pixelCoord] : frameB[pixelCoord];
                break;

            case (uint32_t)FallbackAction::BlendFrame:
            default:
                // Cinematic Motion Blur (Alpha-Blend without vectors)
                float4 colA = frameA[pixelCoord];
                float4 colB = frameB[pixelCoord];
                finalColor = lerp(colA, colB, pc.t);
                break;
        }
    } 
    // 3. Standard interpolation: Bidirectional Warping
    else {
        // Convert offset vector to UV space
        float2 motionUV = motion * pc.invResolution;

        // Vector offsets for frame A and frame B
        // Frame A is projected forward by step t
        // Frame B is projected backward by step (1.0 - t)
        float2 uvA = uv - (motionUV * pc.t);
        float2 uvB = uv + (motionUV * (1.0 - pc.t));

        float4 sampleA = sampleWarped(frameA, uvA);
        float4 sampleB = sampleWarped(frameB, uvB);

        // Weighted blending of motion-compensated frames
        // Weight can also be modulated by local confidence for smooth falloff
        finalColor = lerp(sampleA, sampleB, pc.t);
    }

    // Write to the final buffer
    outputFrame[pixelCoord] = finalColor;
}