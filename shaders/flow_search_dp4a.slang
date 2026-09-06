// PB FrameFlux - LGPL-2.1
// flow_search_dp4a.slang: Wave-Accelerated Block Matching via DP4A

struct SearchPushConstants {
    uint2 blockGridDim;      // Number of blocks (frame resolution / 4)
    uint2 frameResolution;   // Native resolution
    float invPyramidScale;   // Scale of the current pyramid level
};

[[vk::push_constant]]
ConstantBuffer<SearchPushConstants> pc;

// Frame A and B textures: packed luminance (4 horizontal pixels in uint32)
[[vk::binding(0, 0)]] Texture2D<uint>  lumaPackedA;
[[vk::binding(1, 0)]] Texture2D<uint>  lumaPackedB;

// Input vector from the previous, coarser pyramid layer
[[vk::binding(2, 0)]] Texture2D<float2> coarseMotionField;

// Output data: motion vector and confidence
[[vk::binding(3, 0)]] RWTexture2D<float2> outMotionVectors;
[[vk::binding(4, 0)]] RWTexture2D<float>  outConfidenceMap;

// Table of 25 offsets for a 5x5 grid [-2 .. +2]
static const int2 SEARCH_OFFSETS[25] = {
    int2(-2,-2), int2(-1,-2), int2(0,-2), int2(1,-2), int2(2,-2),
    int2(-2,-1), int2(-1,-1), int2(0,-1), int2(1,-1), int2(2,-1),
    int2(-2, 0), int2(-1, 0), int2(0, 0), int2(1, 0), int2(2, 0),
    int2(-2, 1), int2(-1, 1), int2(0, 1), int2(1, 1), int2(2, 1),
    int2(-2, 2), int2(-1, 2), int2(0, 2), int2(1, 2), int2(2, 2)
};


// Compute SAD / SSD error between two 4-pixel rows using DP4A
// SSD expansion: (A - B)^2 = A^2 + B^2 - 2 * (A . B)
uint computeBlockSAD_DP4A(uint packedA, uint packedB) {
    // Slang stdlib intrinsic for VK_KHR_shader_integer_dot_product / DP4A
    // Signature: uint dot4add_u8packed(uint x, uint y, uint acc);
    uint dotAB = dot4add_u8packed(packedA, packedB, 0);
    uint dotAA = dot4add_u8packed(packedA, packedA, 0);
    uint dotBB = dot4add_u8packed(packedB, packedB, 0);

    // Squared difference: sum((A - B)^2)
    int diff = int(dotAA + dotBB) - int(2 * dotAB);
    return uint(max(0, diff));
}

// 1 block = 1 Subgroup (32 threads process 25 candidates in parallel)
[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(
    uint3 groupID : SV_GroupID,
    uint laneIndex : SV_GroupIndex // 0 .. 31 within the wave
) {
    uint2 blockCoord = groupID.xy;

    if (blockCoord.x >= pc.blockGridDim.x || blockCoord.y >= pc.blockGridDim.y) {
        return;
    }

    // 1. Read coarse vector from the previous pyramid pass
    float2 coarseVec = coarseMotionField[blockCoord] * pc.invPyramidScale;
    int2 coarseOffset = int2(round(coarseVec));

    // Center coordinates of the current 4x4 block in frame A
    int2 blockCenterA = int2(blockCoord * 4);

    uint candidateCost = 0xFFFFFFFF;
    int2 candidateOffset = int2(0, 0);

    // 2. The first 25 warp threads each take one offset candidate
    if (laneIndex < 25) {
        candidateOffset = coarseOffset + SEARCH_OFFSETS[laneIndex];
        int2 blockCenterB = blockCenterA + candidateOffset;

        uint accumulatedCost = 0;

        // 4x4 block: 4 rows, each row containing 4 packed luminance bytes in a single uint
        [unroll]
        for (int row = 0; row < 4; ++row) {
            int2 coordA = blockCenterA + int2(0, row);
            int2 coordB = blockCenterB + int2(0, row);

            // Screen bounds check
            if (coordB.x >= 0 && coordB.y >= 0 && 
                coordB.x < (int)pc.frameResolution.x && coordB.y < (int)pc.frameResolution.y) 
            {
                uint packedA = lumaPackedA[coordA / int2(4, 1)];
                uint packedB = lumaPackedB[coordB / int2(4, 1)];

                accumulatedCost += computeBlockSAD_DP4A(packedA, packedB);
            } else {
                accumulatedCost += 50000; // Penalty for going out of frame bounds
            }
        }

        candidateCost = accumulatedCost;
    }

    // 3. Subgroup Reduction - finding the minimum error
    // Instantly find the minimum error among all 25 candidates
    uint minCost = WaveActiveMin(candidateCost);

    // Calculate the average error among valid candidates for Confidence computation
    uint validCost = (laneIndex < 25) ? candidateCost : 0;
    uint sumCost = WaveActiveSum(validCost);
    float avgCost = float(sumCost) / 25.0;

    // 4. Winner thread publishes its motion vector
    bool isWinner = (candidateCost == minCost) && (laneIndex < 25);

    // If multiple threads found the same error, take the one with the lowest index
    uint winnerLane = WaveActiveMin(isWinner ? laneIndex : 32);

    // Compute Confidence (vector reliability):
    // The more minCost stands out against the average error (avgCost), the more reliable the vector.
    // If minCost == avgCost, the image is uniform (sky) or noise -> confidence = 0.
    float confidence = 1.0;
    if (avgCost > 0.001) {
        float ratio = float(minCost) / avgCost;
        confidence = saturate(1.0 - ratio);
    }

    // If the error is still huge even for the winner -> tracking loss / motion breakdown
    if (minCost > 15000) {
        confidence *= 0.2;
    }

    // 5. Only lane 0 writes results to global memory
    if (laneIndex == 0) {
        // Retrieve the offset from the winning thread via register shuffle
        int2 bestOffset = WaveReadLaneAt(candidateOffset, winnerLane);

        outMotionVectors[blockCoord] = float2(bestOffset);
        outConfidenceMap[blockCoord] = confidence;
    }
}