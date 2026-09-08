// PB FrameFlux - LGPL-2.1
// flow_refine_subgroup.slang: Fine-Level Hierarchical Vector Refinement

struct RefinePushConstants {
    uint2 blockGridDim;
    uint2 frameResolution;
};

[[vk::push_constant]]
ConstantBuffer<RefinePushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<uint>    lumaPackedA;
[[vk::binding(1, 0)]] Texture2D<uint>    lumaPackedB;
[[vk::binding(2, 0)]] Texture2D<float2>  coarseMotionField; // Coarse motion from lower pyramid level

[[vk::binding(3, 0)]] RWTexture2D<float2> outFineMotion;
[[vk::binding(4, 0)]] RWTexture2D<float>  outConfidence;

static const int2 REFINE_OFFSETS[9] = {
    int2(-1,-1), int2(0,-1), int2(1,-1),
    int2(-1, 0), int2(0, 0), int2(1, 0),
    int2(-1, 1), int2(0, 1), int2(1, 1)
};

uint extractPackedLumaB(int2 pixelCoord) {
    int clampedX = clamp(pixelCoord.x, 0, (int)pc.frameResolution.x - 4);
    int clampedY = clamp(pixelCoord.y, 0, (int)pc.frameResolution.y - 1);

    int wordIdx = clampedX >> 2;
    int byteShift = clampedX & 3;

    uint w0 = lumaPackedB[int2(wordIdx, clampedY)];
    if (byteShift == 0) return w0;

    int maxWord = (int)((pc.frameResolution.x + 3) / 4) - 1;
    uint w1 = lumaPackedB[int2(min(wordIdx + 1, maxWord), clampedY)];
    return (w0 >> (byteShift * 8)) | (w1 << ((4 - byteShift) * 8));
}

uint computeBlockSAD_DP4A(uint packedA, uint packedB) {
    uint dotAB = dot4add_u8packed(packedA, packedB, 0);
    uint dotAA = dot4add_u8packed(packedA, packedA, 0);
    uint dotBB = dot4add_u8packed(packedB, packedB, 0);
    int diff = int(dotAA + dotBB) - int(2 * dotAB);
    return uint(max(0, diff));
}

[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(uint3 groupID : SV_GroupID, uint laneIndex : SV_GroupIndex) {
    uint2 blockCoord = groupID.xy;
    if (blockCoord.x >= pc.blockGridDim.x || blockCoord.y >= pc.blockGridDim.y) return;

    // Ingest coarse vector (scaled up by 2x from half-res pyramid)
    float2 coarseVec = coarseMotionField[blockCoord / 2] * 2.0;
    int2 coarseOffset = int2(round(coarseVec));

    int2 blockCenterA = int2(blockCoord * 4);
    uint candidateCost = 0xFFFFFFFF;
    int2 candidateOffset = coarseOffset;

    // 9 lanes refine around coarse vector
    if (laneIndex < 9) {
        candidateOffset = coarseOffset + REFINE_OFFSETS[laneIndex];
        int2 blockCenterB = blockCenterA + candidateOffset;

        uint accumulatedCost = 0;
        [unroll]
        for (int row = 0; row < 4; ++row) {
            int2 coordA = blockCenterA + int2(0, row);
            int2 coordB = blockCenterB + int2(0, row);

            uint packedA = lumaPackedA[int2(blockCoord.x, coordA.y)];
            uint packedB = extractPackedLumaB(coordB);

            accumulatedCost += computeBlockSAD_DP4A(packedA, packedB);
        }
        candidateCost = accumulatedCost;
    }

    uint minCost = WaveActiveMin(candidateCost);
    bool isWinner = (candidateCost == minCost) && (laneIndex < 9);
    uint winnerLane = WaveActiveMin(isWinner ? laneIndex : 32);

    if (laneIndex == 0) {
        int2 bestOffset = WaveReadLaneAt(candidateOffset, winnerLane);
        outFineMotion[blockCoord] = float2(bestOffset);
        outConfidence[blockCoord] = 1.0 - saturate(float(minCost) / 15000.0);
    }
}