// PB FrameFlux - LGPL-2.1
// flow_search_dp4a.slang: Zero-Motion Biased Subgroup Matcher (Anti-Drift)

struct SearchPushConstants {
    uint2 blockGridDim;      // Dimensions of block grid (frame resolution / 4)
    uint2 frameResolution;   // Full frame native resolution
    uint32_t searchStride;   // Stride multiplier (1 for standard +-8px, 3 for high +-24px)
    float invPyramidScale;
};

[[vk::push_constant]]
ConstantBuffer<SearchPushConstants> pc;

[[vk::binding(0, 0)]] Texture2D<uint>   lumaPackedA;
[[vk::binding(1, 0)]] Texture2D<uint>   lumaPackedB;
[[vk::binding(2, 0)]] Texture2D<float2> coarseMotionField;

[[vk::binding(3, 0)]] RWTexture2D<float2> outMotionVectors;
[[vk::binding(4, 0)]] RWTexture2D<float>  outConfidenceMap;

// Index 12 is EXACTLY (0, 0) - standing still
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

    int2 blockCenterA = int2(blockCoord * 4);
    int stride = (int)pc.searchStride;

    uint candidateCost = 0xFFFFFFFF;
    int2 candidateOffset = int2(0, 0);

    if (laneIndex < 25) {
        candidateOffset = SEARCH_OFFSETS[laneIndex] * stride;
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

    // 1. Read static cost (Lane 12 is offset (0, 0))
    uint staticCost = WaveReadLaneAt(candidateCost, 12);

    // 2. Find minimum cost among all candidates
    uint minCost = WaveActiveMin(candidateCost);
    uint validCost = (laneIndex < 25) ? candidateCost : 0;
    uint sumCost = WaveActiveSum(validCost);
    float avgCost = float(sumCost) / 25.0;

    // 3. ZERO-MOTION BIAS:
    // If standing still (0, 0) has low cost (< 300) OR is within 15% + 400 of the best moving candidate,
    // we STRICTLY prefer (0, 0) to eliminate hallucinated swimming motion!
    bool preferStatic = (staticCost <= minCost + 400) || (staticCost < 300);

    bool isWinner = false;
    if (preferStatic) {
        // Force Lane 12 (0, 0) as winner
        isWinner = (laneIndex == 12);
    } else {
        isWinner = (candidateCost == minCost) && (laneIndex < 25);
    }

    uint winnerLane = WaveActiveMin(isWinner ? laneIndex : 32);

    float confidence = 1.0;
    if (preferStatic) {
        confidence = 1.0; // 100% confidence in static scene
    } else {
        if (avgCost > 0.001) {
            float ratio = float(minCost) / avgCost;
            confidence = saturate(1.0 - ratio);
        }
        if (minCost > 20000) confidence *= 0.1;
    }

    if (laneIndex == 0) {
        int2 bestOffset = preferStatic ? int2(0, 0) : WaveReadLaneAt(candidateOffset, winnerLane);
        outMotionVectors[blockCoord] = float2(bestOffset);
        outConfidenceMap[blockCoord] = confidence;
    }
}