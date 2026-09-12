// PB FrameFlux - LGPL-2.1
// warp_interpolate.slang: OptiScaler-Style Optical Flow Arrows & Middlebury Visualizer

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

void getNeighborhoodBox(uint2 pixelCoord, out float3 boxMin, out float3 boxMax) {
    int2 center = int2(pixelCoord);
    boxMin = float3(1e5);
    boxMax = float3(-1e5);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 sampleCoord = clamp(center + int2(x, y), int2(0), int2(pc.resolution) - 1);
            float3 c = frameB[sampleCoord].rgb;
            boxMin = min(boxMin, c);
            boxMax = max(boxMax, c);
        }
    }
}

// Расстояние от точки p до отрезка a-b
float distToSegment(float2 p, float2 a, float2 b) {
    float2 pa = p - a, ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 0.0001));
    return length(pa - ba * h);
}

// Каноническое цветовое кодирование оптического потока Middlebury (0 = белый, угол = цвет)
float3 flowToMiddlebury(float2 flow, float maxMotion) {
    float len = length(flow);
    if (len < 0.001) return float3(1.0, 1.0, 1.0);

    float angle = atan2(-flow.y, -flow.x); // [-PI, PI]
    float hue = (angle / 6.2831853) + 0.5; // [0, 1]

    float3 rgb = saturate(abs(frac(hue + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);

    // Центр белый (покой), границы насыщенные
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
    // 0. LIVE DEBUG VISUALIZERS (OPTISCALER ARROWS & MIDDLEBURY FLOW)
    // =========================================================================
    if (pc.showDebugWatermark == 3) {
        // --- А. Компас направлений в правом нижнем углу ---
        uint legendSize = 90;
        uint margin = 24;
        if (pixelCoord.x >= pc.resolution.x - legendSize - margin && pixelCoord.x < pc.resolution.x - margin &&
            pixelCoord.y >= pc.resolution.y - legendSize - margin && pixelCoord.y < pc.resolution.y - margin)
        {
            uint lx = pixelCoord.x - (pc.resolution.x - legendSize - margin);
            uint ly = pixelCoord.y - (pc.resolution.y - legendSize - margin);

            // Граница рамки
            if (lx == 0 || lx == legendSize - 1 || ly == 0 || ly == legendSize - 1) {
                outputFrame[pixelCoord] = float4(0.12, 0.12, 0.12, 1.0);
                return;
            }

            float2 legendCenter = float2(legendSize * 0.5, legendSize * 0.5);
            float2 offset = float2(lx, ly) - legendCenter;
            float dist = length(offset);
            float radius = float(legendSize) * 0.46;

            // Перекрестие и риски шкалы
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

        // --- Б. Пастельный цветовой фон Middlebury ---
        float2 localFlow = getFilteredMotion(uv);
        float3 flowColor = flowToMiddlebury(localFlow, 16.0);
        // Легкое подмешивание исходного кадра для читаемости геометрии
        float3 finalBg = lerp(flowColor, rawB.rgb * flowColor, 0.18);

        // --- В. Сетка векторных стрелок (опрос 3x3 соседних ячеек) ---
        uint gridStep = 22; // Шаг сетки стрелок
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
                    float2 v = flow * 2.4; // Масштабирование вектора для наглядности
                    float vLen = length(v);
                    float2 p1 = p0 + v; // Кончик стрелки
                    float2 dir = v / vLen;
                    float2 norm = float2(-dir.y, dir.x);

                    // Геометрия наконечника стрелки (V-образные усики)
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

        // Отрисовка черных четких стрелок с антиалиасингом
        if (minArrowDist < 1.0) {
            float alpha = saturate(1.0 - (minArrowDist - 0.4));
            float3 arrowColor = float3(0.04, 0.04, 0.06);
            finalBg = lerp(finalBg, arrowColor, alpha);
        } else if (minDotDist < 1.2) {
            // Точка покоя в центре ячейки
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
    // 1. STATIC UI & INTERFACE PROTECTION
    // =========================================================================
    float2 motionCheck = getFilteredMotion(uv);
    float colorDiff = distance(rawA.rgb, rawB.rgb);

    if (colorDiff < 0.035 && length(motionCheck) < 1.0) {
        outputFrame[pixelCoord] = (pc.t < 0.5) ? rawA : rawB;
        return;
    }

    // =========================================================================
    // 2. TRANSIENT PARTICLE & RAIN DETECTION
    // =========================================================================
    int2 coord = int2(pixelCoord);
    int2 clampMax = int2(pc.resolution) - 1;

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

    bool isRainParticleB = (localContrastB > 0.075 && localContrastA < 0.025);
    bool isRainParticleA = (localContrastA > 0.075 && localContrastB < 0.025);

    if (isRainParticleB) {
        outputFrame[pixelCoord] = (pc.t < 0.35) ? rawA : lerp(rawA, rawB, smoothstep(0.35, 1.0, pc.t));
        return;
    }

    if (isRainParticleA) {
        outputFrame[pixelCoord] = (pc.t > 0.65) ? rawB : lerp(rawA, rawB, smoothstep(0.0, 0.65, pc.t));
        return;
    }

    // =========================================================================
    // 3. MOTION-COMPENSATED WARPING
    // =========================================================================
    float2 motion = motionCheck;
    float confidence = confidenceMap.SampleLevel(linearSampler, uv, 0.0);

    if (confidence < pc.confidenceThreshold) {
        if (pc.fallbackAction == FallbackAction::BlendFrame) {
            outputFrame[pixelCoord] = lerp(rawA, rawB, 0.5);
        } else {
            outputFrame[pixelCoord] = (pc.t < 0.5) ? rawA : rawB;
        }
        return;
    }

    float2 motionUV = motion * pc.invResolution;
    float2 uvA = uv - (motionUV * pc.t);
    float2 uvB = uv + (motionUV * (1.0 - pc.t));

    float4 sampleA = sampleColor(frameA, uvA);
    float4 sampleB = sampleColor(frameB, uvB);

    // =========================================================================
    // 4. DISOCCLUSION CHECK & COLOR BOX CLAMPING
    // =========================================================================
    float lumaA = dot(sampleA.rgb, float3(0.299, 0.587, 0.114));
    float lumaB = dot(sampleB.rgb, float3(0.299, 0.587, 0.114));
    float photometricDiff = abs(lumaA - lumaB);

    float4 finalColor;
    if (photometricDiff > 0.20) {
        finalColor = (pc.t < 0.5) ? sampleA : sampleB;
    } else {
        float4 interpolated = lerp(sampleA, sampleB, pc.t);

        float3 boxMin, boxMax;
        getNeighborhoodBox(pixelCoord, boxMin, boxMax);
        float3 clampedRgb = clamp(interpolated.rgb, boxMin - float3(0.03), boxMax + float3(0.03));

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