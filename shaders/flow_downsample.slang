// PB FrameFlux - LGPL-2.1
// flow_downsample.slang: 2x Hierarchical Luma Pyramid Downsampler

struct DownsamplePushConstants {
    uint2 srcResolution; // Native resolution
    uint2 dstResolution; // Half resolution
};

[[vk::push_constant]]
ConstantBuffer<DownsamplePushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<uint>   srcLumaPacked;  // Full-res packed 4x1 luma
[[vk::binding(1, 0)]] RWTexture2D<uint> dstLumaPacked;  // Half-res packed 4x1 luma

// Extracts individual 8-bit luma byte from a 4-pixel packed uint
uint getByte(uint packedVal, uint idx) {
    return (packedVal >> (idx * 8)) & 0xFF;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 dstPackedCoord = dispatchThreadID.xy;
    if (dstPackedCoord.x >= pc.dstResolution.x || dstPackedCoord.y >= pc.dstResolution.y) return;

    // Each half-res packed uint represents 4 pixels horizontally (8 source pixels)
    // and averages 2 rows vertically
    uint srcBaseX = dstPackedCoord.x * 2; // In packed units
    uint srcBaseY = dstPackedCoord.y * 2;

    uint row0_w0 = srcLumaPacked[uint2(srcBaseX + 0, srcBaseY + 0)];
    uint row0_w1 = srcLumaPacked[uint2(srcBaseX + 1, srcBaseY + 0)];
    uint row1_w0 = srcLumaPacked[uint2(srcBaseX + 0, srcBaseY + 1)];
    uint row1_w1 = srcLumaPacked[uint2(srcBaseX + 1, srcBaseY + 1)];

    // Average 2x2 blocks into 1 pixel
    uint p0 = (getByte(row0_w0, 0) + getByte(row0_w0, 1) + getByte(row1_w0, 0) + getByte(row1_w0, 1)) / 4;
    uint p1 = (getByte(row0_w0, 2) + getByte(row0_w0, 3) + getByte(row1_w0, 2) + getByte(row1_w0, 3)) / 4;
    uint p2 = (getByte(row0_w1, 0) + getByte(row0_w1, 1) + getByte(row1_w1, 0) + getByte(row1_w1, 1)) / 4;
    uint p3 = (getByte(row0_w1, 2) + getByte(row0_w1, 3) + getByte(row1_w1, 2) + getByte(row1_w1, 3)) / 4;

    uint packedHalf = p0 | (p1 << 8) | (p2 << 16) | (p3 << 24);
    dstLumaPacked[dstPackedCoord] = packedHalf;
}