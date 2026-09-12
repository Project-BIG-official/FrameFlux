// PB FrameFlux - LGPL-2.1
// flow_search_dp4a.slang: High-Throughput Register-Cached Motion Estimator

struct SearchPushConstants {
    uint2 blockGridDim;      
    uint2 frameResolution;   
    uint32_t searchStride;   
    float invPyramidScale;
};

[[vk::push_constant]]
ConstantBuffer<SearchPushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<uint>   lumaPackedA;
[[vk::binding(1, 0)]] Texture2D<uint>   lumaPackedB;
[[vk::binding(2, 0)]] Texture2D<float2> coarseMotionField;

[[vk::binding(3, 0)]] RWTexture2D<float2> outMotionVectors;
[[vk::binding(4, 0)]] RWTexture2D<float>  outConfidenceMap;

static const int2 SEARCH_OFFSETS[25] = {
    int2(-2,-2), int2(-1,-2), int2(0,-2), int2(1,-2), int2(2,-2),
    int2(-2,-1), int2(-1,-1), int2(0,-1), int2(1,-1), int2(2,-1),
    int2(-2, 0), int2(-1, 0), int2(0, 0), int2(1, 0), int2(2, 0),
    int2(-2, 1), int2(-1, 1), int2(0, 1), int2(1, 1), int2(2, 1),
    int2(-2, 2), int2(-1, 2), int2(0, 2), int2(1, 2), int2(2, 2)
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

uint computeBlockSSD_DP4A(uint packedA, uint packedB) {
    uint dotAB = dot4add_u8packed(packedA, packedB, 0);
    uint dotAA = dot4add_u8packed(packedA, packedA, 0);
    uint dotBB = dot4add_u8packed(packedB, packedB, 0);
    int diff = int(dotAA + dotBB) - int(2 * dotAB);
    return uint(max(0, diff));
}

[shader("compute")]
[numthreads(32, 1, 1)]
[WaveSize(32)]
void computeMain(uint3 groupID : SV_GroupID, uint laneIndex : SV_GroupIndex) {
    uint2 blockCoord = groupID.xy;
    if (blockCoord.x >= pc.blockGridDim.x || blockCoord.y >= pc.blockGridDim.y) return;

    int2 blockCenterA = int2(blockCoord * 4);
    int effectiveStride = max((int)pc.searchStride * 2, 2);

    // OPTIMIZATION: Cache rows of packed block A into local VGPR registers once
    uint cachedPackedA[4];
    [unroll]
    for (int r = 0; r < 4; ++r) {
        cachedPackedA[r] = lumaPackedA[int2(blockCoord.x, blockCenterA.y + r)];
    }

    uint candidateCost = 0xFFFFFFFF;
    int2 candidateOffset = int2(0, 0);

    if (laneIndex < 25) {
        candidateOffset = SEARCH_OFFSETS[laneIndex] * effectiveStride;
        int2 blockCenterB = blockCenterA + candidateOffset;

        uint accumulatedCost = 0;
        [unroll]
        for (int row = 0; row < 4; ++row) {
            int2 coordB = blockCenterB + int2(0, row);
            uint packedB = extractPackedLumaB(coordB);
            accumulatedCost += computeBlockSSD_DP4A(cachedPackedA[row], packedB);
        }
        candidateCost = accumulatedCost;
    }

    uint staticCost = WaveReadLaneAt(candidateCost, 12);
    uint minCost = WaveActiveMin(candidateCost);

    // Zero-motion bias
    bool preferStatic = (staticCost <= minCost + 800) || (staticCost < 400);

    bool isWinner = false;
    if (preferStatic) {
        isWinner = (laneIndex == 12);
    } else {
        isWinner = (candidateCost == minCost) && (laneIndex < 25);
    }

    uint winnerLane = WaveActiveMin(isWinner ? laneIndex : 32);

    float avgPixelErrorSq = float(minCost) / 16.0;
    float confidence = saturate(1.0 - (avgPixelErrorSq / 2500.0));

    if (preferStatic) {
        confidence = max(confidence, 0.98);
    }

    if (laneIndex == 0) {
        int2 bestOffset = preferStatic ? int2(0, 0) : WaveReadLaneAt(candidateOffset, winnerLane);
        outMotionVectors[blockCoord] = float2(bestOffset);
        outConfidenceMap[blockCoord] = confidence;
    }
}