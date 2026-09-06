// PB FrameFlux - LGPL-2.1
// luma_pack.slang: Converts RGBA frame into 4-in-1 packed 8-bit luma for DP4A

struct LumaPushConstants {
    uint2 resolution;
};

[[vk::push_constant]]
ConstantBuffer<LumaPushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<float4>   inputColor;
[[vk::binding(1, 0)]] RWTexture2D<uint>   outputPackedLuma;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 packedCoord = dispatchThreadID.xy;
    uint baseX = packedCoord.x * 4;
    uint y = packedCoord.y;

    if (baseX >= pc.resolution.x || y >= pc.resolution.y) {
        return;
    }

    // Standard ITU-R BT.601 luma weights
    float3 weights = float3(0.299, 0.587, 0.114);

    uint p0 = 0, p1 = 0, p2 = 0, p3 = 0;

    if (baseX + 0 < pc.resolution.x) p0 = uint(clamp(dot(inputColor[uint2(baseX + 0, y)].rgb, weights) * 255.0, 0.0, 255.0));
    if (baseX + 1 < pc.resolution.x) p1 = uint(clamp(dot(inputColor[uint2(baseX + 1, y)].rgb, weights) * 255.0, 0.0, 255.0));
    if (baseX + 2 < pc.resolution.x) p2 = uint(clamp(dot(inputColor[uint2(baseX + 2, y)].rgb, weights) * 255.0, 0.0, 255.0));
    if (baseX + 3 < pc.resolution.x) p3 = uint(clamp(dot(inputColor[uint2(baseX + 3, y)].rgb, weights) * 255.0, 0.0, 255.0));

    // Pack 4 pixels into a single 32-bit unsigned integer (byte 0..3)
    uint packedVal = (p0) | (p1 << 8) | (p2 << 16) | (p3 << 24);

    outputPackedLuma[packedCoord] = packedVal;
}