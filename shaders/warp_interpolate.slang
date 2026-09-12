// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: Ghost-Free Warping with Zero-Blend Disocclusion & UI Edge Locking

enum FallbackAction : uint {
    RepeatFrame = 0,
    BlendFrame  = 1,
    DropFrame   = 2
};

struct PushConstants {
    float t;
    float confidenceThreshold;
    uint fallbackAction;
    uint showDebugWatermark;
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

float4 sampleColor(Texture2D<float4> tex, float2 uv) {
    float2 clampedUV = clamp(uv, float2(0.0001), float2(0.9999));
    return tex.SampleLevel(linearSampler, clampedUV, 0.0);
}

float2 getFilteredMotion(float2 uv) {
    float2 offsets[5] = {
        float2(0.0, 0.0),
        float2(-1.5,  0.0) * pc.invResolution,
        float2( 1.5,  0.0) * pc.invResolution,
        float2( 0.0, -1.5) * pc.invResolution,
        float2( 0.0,  1.5) * pc.invResolution
    };

    float2 v0 = motionVectors.SampleLevel(linearSampler, uv + offsets[0], 0.0);
    float2 v1 = motionVectors.SampleLevel(linearSampler, uv + offsets[1], 0.0);
    float2 v2 = motionVectors.SampleLevel(linearSampler, uv + offsets[2], 0.0);
    float2 v3 = motionVectors.SampleLevel(linearSampler, uv + offsets[3], 0.0);
    float2 v4 = motionVectors.SampleLevel(linearSampler, uv + offsets[4], 0.0);

    float2 min1 = min(v0, v1); float2 max1 = max(v0, v1);
    float2 min2 = min(v2, v3); float2 max2 = max(v2, v3);
    float2 midMin = max(min1, min2);
    float2 midMax = min(max1, max2);
    return clamp(v4, min(midMin, midMax), max(midMin, midMax));
}

void getNeighborhoodBoxBi(uint2 pixelCoord, out float3 boxMin, out float3 boxMax) {
    int2 center = int2(pixelCoord);
    int2 maxCoord = int2(pc.resolution) - 1;
    boxMin = float3(1e5);
    boxMax = float3(-1e5);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 sampleCoord = clamp(center + int2(x, y), int2(0), maxCoord);
            float3 cA = frameA[sampleCoord].rgb;
            float3 cB = frameB[sampleCoord].rgb;
            boxMin = min(boxMin, min(cA, cB));
            boxMax = max(boxMax, max(cA, cB));
        }
    }
}

float distToSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a, ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 0.0001));
    return length(pa - ba * h);
}

float3 flowToMiddlebury(float2 flow, float maxMotion) {
    float len = length(flow);
    if (len < 0.001) return float3(1.0, 1.0, 1.0);

    float angle = atan2(-flow.y, -flow.x);
    float hue = (angle / 6.2831853) + 0.5;

    float3 rgb = saturate(abs(frac(hue + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);

    float s = saturate(len / maxMotion);
    float satCurve = pow(s, 0.7);
    return lerp(float3(1.0, 1.0, 1.0), rgb, satCurve);
}

float3 confidenceToHeatmap(float conf, float3 bgRgb) {
    float3 heatColor;
    if (conf > 0.5) {
        heatColor = lerp(float3(1.0, 0.85, 0.0), float3(0.0, 1.0, 0.25), (conf - 0.5) * 2.0);
    } else {
        heatColor = lerp(float3(0.95, 0.05, 0.15), float3(1.0, 0.85, 0.0), conf * 2.0);
    }
    float mono = dot(bgRgb, float3(0.299, 0.587, 0.114));
    return lerp(float3(mono * 0.25), heatColor, 0.85);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolution.x || pixelCoord.y >= pc.resolution.y) return;

    float2 uv = (float2(pixelCoord) + 0.5) * pc.invResolution;
    float4 rawA = frameA[pixelCoord];
    float4 rawB = frameB[pixelCoord];

    // =========================================================================
    // 0. LIVE DEBUG VISUALIZERS
    // =========================================================================
    if (pc.showDebugWatermark == 3) {
        uint legendSize = 90;
        uint margin = 24;
        if (pixelCoord.x >= pc.resolution.x - legendSize - margin && pixelCoord.x < pc.resolution.x - margin &&
            pixelCoord.y >= pc.resolution.y - legendSize - margin && pixelCoord.y < pc.resolution.y - margin)
        {
            uint lx = pixelCoord.x - (pc.resolution.x - legendSize - margin);
            uint ly = pixelCoord.y - (pc.resolution.y - legendSize - margin);

            if (lx == 0 || lx == legendSize - 1 || ly == 0 || ly == legendSize - 1) {
                outputFrame[pixelCoord] = float4(0.12, 0.12, 0.12, 1.0);
                return;
            }

            float2 legendCenter = float2(legendSize * 0.5, legendSize * 0.5);
            float2 offset = float2(lx, ly) - legendCenter;
            float dist = length(offset);
            float radius = float(legendSize) * 0.46;

            bool isCrosshair = (abs(offset.x) < 0.75 || abs(offset.y) < 0.75) && (dist < radius);
            bool isTick = (abs(dist - radius * 0.5) < 0.75) && (abs(offset.x) < 3.0 || abs(offset.y) < 3.0);

            if (isCrosshair || isTick) {
                outputFrame[pixelCoord] = float4(0.15, 0.15, 0.15, 1.0);
                return;
            }

            if (dist <= radius) {
                float3 wheelCol = flowToMiddlebury(offset, radius);
                outputFrame[pixelCoord] = float4(wheelCol, 1.0);
                return;
            } else {
                outputFrame[pixelCoord] = float4(0.08, 0.08, 0.1, 0.95);
                return;
            }
        }

        float2 localFlow = getFilteredMotion(uv);
        float3 flowColor = flowToMiddlebury(localFlow, 16.0);
        float3 finalBg = lerp(flowColor, rawB.rgb * flowColor, 0.18);

        uint gridStep = 22;
        int2 baseCell = int2(floor(float2(pixelCoord) / float2(gridStep)));
        float minArrowDist = 999.0;
        float minDotDist = 999.0;

        [unroll]
        for (int dy = -1; dy <= 1; ++dy) {
            [unroll]
            for (int dx = -1; dx <= 1; ++dx) {
                int2 cell = baseCell + int2(dx, dy);
                float2 p0 = (float2(cell) + 0.5) * float2(gridStep);
                if (p0.x < 0.0 || p0.x >= float(pc.resolution.x) || p0.y < 0.0 || p0.y >= float(pc.resolution.y)) continue;

                float2 sampleUV = p0 * pc.invResolution;
                float2 flow = motionVectors.SampleLevel(linearSampler, sampleUV, 0.0);
                float flowLen = length(flow);

                if (flowLen > 0.4) {
                    float2 v = flow * 2.4;
                    float vLen = length(v);
                    float2 p1 = p0 + v;
                    float2 dir = v / vLen;
                    float2 norm = float2(-dir.y, dir.x);

                    float headLen = clamp(vLen * 0.35, 3.5, 7.5);
                    float headWidth = headLen * 0.55;
                    float2 headBase = p1 - dir * headLen;
                    float2 w1 = headBase + norm * headWidth;
                    float2 w2 = headBase - norm * headWidth;

                    float dShaft = distToSegment(float2(pixelCoord), p0, p1);
                    float dW1 = distToSegment(float2(pixelCoord), p1, w1);
                    float dW2 = distToSegment(float2(pixelCoord), p1, w2);

                    float dArrow = min(dShaft, min(dW1, dW2));
                    minArrowDist = min(minArrowDist, dArrow);
                } else {
                    minDotDist = min(minDotDist, length(float2(pixelCoord) - p0));
                }
            }
        }

        if (minArrowDist < 1.0) {
            float alpha = saturate(1.0 - (minArrowDist - 0.4));
            float3 arrowColor = float3(0.04, 0.04, 0.06);
            finalBg = lerp(finalBg, arrowColor, alpha);
        } else if (minDotDist < 1.2) {
            finalBg = lerp(finalBg, float3(0.35, 0.35, 0.4), 0.7);
        }

        outputFrame[pixelCoord] = float4(finalBg, 1.0);
        return;
    }

    if (pc.showDebugWatermark == 2) {
        float conf = confidenceMap.SampleLevel(linearSampler, uv, 0.0);
        outputFrame[pixelCoord] = float4(confidenceToHeatmap(conf, rawB.rgb), 1.0);
        return;
    }

    // =========================================================================
    // 1. ЗАЩИТА СТАТИЧЕСКИХ ГРАНИЦ И МЕНЮ (ПОЛНОЕ ИСКЛЮЧЕНИЕ ДВОЕНИЯ КАРТОЧЕК)
    // =========================================================================
    int2 coord = int2(pixelCoord);
    int2 clampMax = int2(pc.resolution) - 1;

    float lumaCenterA = dot(rawA.rgb, float3(0.299, 0.587, 0.114));
    float lumaCenterB = dot(rawB.rgb, float3(0.299, 0.587, 0.114));

    float lumaRA = dot(frameA[clamp(coord + int2(1, 0), int2(0), clampMax)].rgb, float3(0.299, 0.587, 0.114));
    float lumaDA = dot(frameA[clamp(coord + int2(0, 1), int2(0), clampMax)].rgb, float3(0.299, 0.587, 0.114));
    float lumaRB = dot(frameB[clamp(coord + int2(1, 0), int2(0), clampMax)].rgb, float3(0.299, 0.587, 0.114));
    float lumaDB = dot(frameB[clamp(coord + int2(0, 1), int2(0), clampMax)].rgb, float3(0.299, 0.587, 0.114));

    float gradA = abs(lumaCenterA - lumaRA) + abs(lumaCenterA - lumaDA);
    float gradB = abs(lumaCenterB - lumaRB) + abs(lumaCenterB - lumaDB);

    // Граница статична, если ее координаты и резкость совпадают в обоих кадрах
    bool isStaticUI = (gradA > 0.05 && gradB > 0.05 && abs(gradA - gradB) < 0.04);
    float colorDiff = distance(rawA.rgb, rawB.rgb);

    if (isStaticUI || colorDiff < 0.02) {
        outputFrame[pixelCoord] = rawB;
        return;
    }

    // =========================================================================
    // 2. ДЕТЕКТОР ЧАСТИЦ
    // =========================================================================
    float4 nbL = frameB[clamp(coord + int2(-1,  0), int2(0), clampMax)];
    float4 nbR = frameB[clamp(coord + int2( 1,  0), int2(0), clampMax)];
    float4 nbU = frameB[clamp(coord + int2( 0, -1), int2(0), clampMax)];
    float4 nbD = frameB[clamp(coord + int2( 0,  1), int2(0), clampMax)];
    float3 avgBNeighbors = (nbL.rgb + nbR.rgb + nbU.rgb + nbD.rgb) * 0.25;

    float4 naL = frameA[clamp(coord + int2(-1,  0), int2(0), clampMax)];
    float4 naR = frameA[clamp(coord + int2( 1,  0), int2(0), clampMax)];
    float4 naU = frameA[clamp(coord + int2( 0, -1), int2(0), clampMax)];
    float4 naD = frameA[clamp(coord + int2( 0,  1), int2(0), clampMax)];
    float3 avgANeighbors = (naL.rgb + naR.rgb + naU.rgb + naD.rgb) * 0.25;

    float localContrastB = length(rawB.rgb - avgBNeighbors);
    float localContrastA = length(rawA.rgb - avgANeighbors);

    if (localContrastB > 0.075 && localContrastA < 0.025) {
        outputFrame[pixelCoord] = (pc.t < 0.35) ? rawA : lerp(rawA, rawB, smoothstep(0.35, 1.0, pc.t));
        return;
    }
    if (localContrastA > 0.075 && localContrastB < 0.025) {
        outputFrame[pixelCoord] = (pc.t > 0.65) ? rawB : lerp(rawA, rawB, smoothstep(0.0, 0.65, pc.t));
        return;
    }

    // =========================================================================
    // 3. ЧИСТЫЙ ВАРПИНГ БЕЗ ПАРАЗИТНОГО ДВОЕНИЯ
    // =========================================================================
    float2 motion = getFilteredMotion(uv);
    float confidence = confidenceMap.SampleLevel(linearSampler, uv, 0.0);

    float2 motionUV = motion * pc.invResolution;
    float2 uvA = uv - (motionUV * pc.t);
    float2 uvB = uv + (motionUV * (1.0 - pc.t));

    float4 sampleA = sampleColor(frameA, uvA);
    float4 sampleB = sampleColor(frameB, uvB);

    float lumaA = dot(sampleA.rgb, float3(0.299, 0.587, 0.114));
    float lumaB = dot(sampleB.rgb, float3(0.299, 0.587, 0.114));
    float photometricDiff = abs(lumaA - lumaB);

    float motionSpeed = length(motion);
    float disocclusionThreshold = lerp(0.06, 0.16, saturate(motionSpeed / 10.0));

    float4 finalColor;
    if (confidence < pc.confidenceThreshold || photometricDiff > disocclusionThreshold) {
        // При расхождении цветов жестко берем ближайший сэмпл без полупрозрачного двоения
        if (pc.fallbackAction == FallbackAction::BlendFrame) {
            finalColor = lerp(rawA, rawB, pc.t);
        } else {
            finalColor = (pc.t < 0.5) ? rawA : rawB;
        }
    } else {
        // Плавная интерполяция со строгим ограничением палитры
        float4 interpolated = lerp(sampleA, sampleB, pc.t);
        float3 boxMin, boxMax;
        getNeighborhoodBoxBi(pixelCoord, boxMin, boxMax);
        float3 clampedRgb = clamp(interpolated.rgb, boxMin, boxMax);

        finalColor = float4(clampedRgb, interpolated.a);
    }

    if (pc.showDebugWatermark == 1) {
        uint badgeX = pc.resolution.x - 56;
        uint badgeY = 16;
        if (pixelCoord.x >= badgeX && pixelCoord.x < badgeX + 40 &&
            pixelCoord.y >= badgeY && pixelCoord.y < badgeY + 18) 
        {
            uint bx = pixelCoord.x - badgeX;
            uint by = pixelCoord.y - badgeY;
            if (bx == 0 || bx == 39 || by == 0 || by == 17) {
                finalColor = float4(0.0, 1.0, 0.5, 1.0);
            } else {
                finalColor = float4(0.01, 0.18, 0.08, 0.95);
            }
        }
    }

    outputFrame[pixelCoord] = finalColor;
}