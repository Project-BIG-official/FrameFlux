// PB FrameFlux - LGPL-2.1
// overlay_hud.slang: Dynamic Telemetry & Centered Menu with Complete Font & Metrics

struct OverlayPushConstants {
    uint resolutionX;    // Offset 0
    uint resolutionY;    // Offset 4
    uint isMenuOpen;     // Offset 8
    uint isHudVisible;   // Offset 12
    uint selectedItem;   // Offset 16
    uint mode;           // Offset 20
    uint multiplier;     // Offset 24
    uint profile;        // Offset 28
    uint searchMode;     // Offset 32
    uint schedulerMode;  // Offset 36
    uint targetFps;      // Offset 40
    uint fallbackAction; // Offset 44
    uint hudCorner;      // Offset 48
    uint nativeFpsX10;   // Offset 52
    uint outputFpsX10;   // Offset 56
    uint gpuTimeUs;      // Offset 60
    uint cpuTimeUs;      // Offset 64
    uint latencyUs;      // Offset 68
    uint confidenceX10;  // Offset 72
    uint fallbackX10;    // Offset 76
    uint vramMb;         // Offset 80
};

[[vk::push_constant]]
ConstantBuffer<OverlayPushConstants> pc;

[[vk::binding(0, 0)]] RWTexture2D<float4> targetFrame;

uint getFontColumn(uint c, uint col) {
    if (col >= 5) return 0;
    
    if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }

    switch (c) {
        // Digits 0-9
        case '0': { const uint d[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E}; return d[col]; }
        case '1': { const uint d[5] = {0x00, 0x42, 0x7F, 0x40, 0x00}; return d[col]; }
        case '2': { const uint d[5] = {0x42, 0x61, 0x51, 0x49, 0x46}; return d[col]; }
        case '3': { const uint d[5] = {0x21, 0x41, 0x45, 0x4B, 0x31}; return d[col]; }
        case '4': { const uint d[5] = {0x18, 0x14, 0x12, 0x7F, 0x10}; return d[col]; }
        case '5': { const uint d[5] = {0x27, 0x45, 0x45, 0x45, 0x39}; return d[col]; }
        case '6': { const uint d[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30}; return d[col]; }
        case '7': { const uint d[5] = {0x01, 0x71, 0x09, 0x05, 0x03}; return d[col]; }
        case '8': { const uint d[5] = {0x36, 0x49, 0x49, 0x49, 0x36}; return d[col]; }
        case '9': { const uint d[5] = {0x06, 0x49, 0x49, 0x29, 0x1E}; return d[col]; }

        // Complete Alphabet A-Z
        case 'A': { const uint d[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E}; return d[col]; }
        case 'B': { const uint d[5] = {0x7F, 0x49, 0x49, 0x49, 0x36}; return d[col]; }
        case 'C': { const uint d[5] = {0x3E, 0x41, 0x41, 0x41, 0x22}; return d[col]; }
        case 'D': { const uint d[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C}; return d[col]; }
        case 'E': { const uint d[5] = {0x7F, 0x49, 0x49, 0x49, 0x41}; return d[col]; }
        case 'F': { const uint d[5] = {0x7F, 0x09, 0x09, 0x09, 0x01}; return d[col]; }
        case 'G': { const uint d[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A}; return d[col]; }
        case 'H': { const uint d[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F}; return d[col]; }
        case 'I': { const uint d[5] = {0x00, 0x41, 0x7F, 0x41, 0x00}; return d[col]; }
        case 'J': { const uint d[5] = {0x20, 0x40, 0x41, 0x3F, 0x01}; return d[col]; }
        case 'K': { const uint d[5] = {0x7F, 0x08, 0x14, 0x22, 0x41}; return d[col]; }
        case 'L': { const uint d[5] = {0x7F, 0x40, 0x40, 0x40, 0x40}; return d[col]; }
        case 'M': { const uint d[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F}; return d[col]; }
        case 'N': { const uint d[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F}; return d[col]; }
        case 'O': { const uint d[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E}; return d[col]; }
        case 'P': { const uint d[5] = {0x7F, 0x09, 0x09, 0x09, 0x06}; return d[col]; }
        case 'Q': { const uint d[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E}; return d[col]; }
        case 'R': { const uint d[5] = {0x7F, 0x09, 0x19, 0x29, 0x46}; return d[col]; }
        case 'S': { const uint d[5] = {0x46, 0x49, 0x49, 0x49, 0x31}; return d[col]; }
        case 'T': { const uint d[5] = {0x01, 0x01, 0x7F, 0x01, 0x01}; return d[col]; }
        case 'U': { const uint d[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F}; return d[col]; }
        case 'V': { const uint d[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F}; return d[col]; }
        case 'W': { const uint d[5] = {0x7F, 0x20, 0x18, 0x20, 0x7F}; return d[col]; }
        case 'X': { const uint d[5] = {0x63, 0x14, 0x08, 0x14, 0x63}; return d[col]; }
        case 'Y': { const uint d[5] = {0x07, 0x08, 0x70, 0x08, 0x07}; return d[col]; }
        case 'Z': { const uint d[5] = {0x61, 0x51, 0x49, 0x45, 0x43}; return d[col]; }

        // Symbols
        case '.': { const uint d[5] = {0x00, 0x60, 0x60, 0x00, 0x00}; return d[col]; }
        case ':': { const uint d[5] = {0x00, 0x36, 0x36, 0x00, 0x00}; return d[col]; }
        case ',': { const uint d[5] = {0x00, 0x40, 0x60, 0x00, 0x00}; return d[col]; }
        case '!': { const uint d[5] = {0x00, 0x00, 0x5F, 0x00, 0x00}; return d[col]; }
        case '?': { const uint d[5] = {0x02, 0x01, 0x51, 0x09, 0x06}; return d[col]; }
        case '%': { const uint d[5] = {0x23, 0x13, 0x08, 0x64, 0x62}; return d[col]; }
        case '+': { const uint d[5] = {0x08, 0x08, 0x3E, 0x08, 0x08}; return d[col]; }
        case '-': { const uint d[5] = {0x08, 0x08, 0x08, 0x08, 0x08}; return d[col]; }
        case '/': { const uint d[5] = {0x40, 0x20, 0x10, 0x08, 0x04}; return d[col]; }
        case '=': { const uint d[5] = {0x14, 0x14, 0x14, 0x14, 0x14}; return d[col]; }
        case '_': { const uint d[5] = {0x40, 0x40, 0x40, 0x40, 0x40}; return d[col]; }
        case '(': { const uint d[5] = {0x00, 0x1C, 0x22, 0x41, 0x00}; return d[col]; }
        case ')': { const uint d[5] = {0x00, 0x41, 0x22, 0x1C, 0x00}; return d[col]; }
        case '[': { const uint d[5] = {0x00, 0x7F, 0x41, 0x41, 0x00}; return d[col]; }
        case ']': { const uint d[5] = {0x00, 0x41, 0x41, 0x7F, 0x00}; return d[col]; }
        case '<': { const uint d[5] = {0x10, 0x28, 0x44, 0x00, 0x00}; return d[col]; }
        case '>': { const uint d[5] = {0x44, 0x28, 0x10, 0x00, 0x00}; return d[col]; }
        default:  return 0;
    }
}

uint getHudChar(uint line, uint col, OverlayPushConstants cpc) {
    if (line == 0) {
        const uint t[13] = {'F','R','A','M','E','F','L','U','X',' ','2','.','1'};
        return (col < 13) ? t[col] : ' ';
    }
    if (col < 9) {
        if (line == 1) { const uint l[9] = {'N','A','T','I','V','E',':',' ',' '}; return l[col]; }
        if (line == 2) { const uint l[9] = {'G','E','N',':',' ',' ',' ',' ',' '}; return l[col]; }
        if (line == 3) { const uint l[9] = {'O','U','T','P','U','T',':',' ',' '}; return l[col]; }
        if (line == 4) { const uint l[9] = {'G','P','U',' ','F','G',':',' ',' '}; return l[col]; }
        if (line == 5) { const uint l[9] = {'C','P','U',':',' ',' ',' ',' ',' '}; return l[col]; }
        if (line == 6) { const uint l[9] = {'P','A','C','I','N','G',':',' ',' '}; return l[col]; }
        if (line == 7) { const uint l[9] = {'C','O','N','F',':',' ',' ',' ',' '}; return l[col]; }
        if (line == 8) { const uint l[9] = {'F','A','L','L','B',':',' ',' ',' '}; return l[col]; }
        if (line == 9) { const uint l[9] = {'V','R','A','M',':',' ',' ',' ',' '}; return l[col]; }
    }
    if (col >= 9 && col < 15) {
        uint val = 0;
        switch (line) {
            case 1: val = cpc.nativeFpsX10; break;
            case 2: val = (cpc.outputFpsX10 > cpc.nativeFpsX10) ? (cpc.outputFpsX10 - cpc.nativeFpsX10) : 0; break;
            case 3: val = cpc.outputFpsX10; break;
            case 4: val = (cpc.gpuTimeUs + 50) / 100; break; // Microseconds to 0.1ms resolution
            case 5: val = (cpc.cpuTimeUs + 50) / 100; break; // Microseconds to 0.1ms resolution
            case 6: val = (cpc.latencyUs + 50) / 100; break; // Microseconds to 0.1ms resolution
            case 7: val = cpc.confidenceX10; break;          // Percent * 10
            case 8: val = cpc.fallbackX10; break;            // Percent * 10
            case 9: val = cpc.vramMb * 10; break;            // MB * 10
        }

        uint d0 = (val / 1000) % 10;
        uint d1 = (val / 100) % 10;
        uint d2 = (val / 10) % 10;
        uint d3 = val % 10;
        uint numCol = col - 9;

        if (numCol == 0) return (d0 > 0) ? ('0' + d0) : ' ';
        if (numCol == 1) return (d0 > 0 || d1 > 0) ? ('0' + d1) : ' ';
        if (numCol == 2) return '0' + d2;
        if (numCol == 3) return '.';
        if (numCol == 4) return '0' + d3;
        return ' ';
    }
    if (col >= 15 && col < 19) {
        uint uCol = col - 15;
        if (line >= 1 && line <= 3) { const uint u[4] = {'F','P','S',' '}; return u[uCol]; }
        if (line >= 4 && line <= 6) { const uint u[4] = {'M','S',' ',' '}; return u[uCol]; }
        if (line >= 7 && line <= 8) { const uint u[4] = {'%',' ',' ',' '}; return u[uCol]; }
        if (line == 9)              { const uint u[4] = {'M','B',' ',' '}; return u[uCol]; }
    }
    return ' ';
}

uint getCenteredMenuChar(uint line, uint col, OverlayPushConstants cpc) {
    if (line == 0) {
        const uint h[28] = {'[','-','-',' ','P','B',' ','F','R','A','M','E','F','L','U','X',' ','M','E','N','U',' ','-','-',']'};
        return (col < 26) ? h[col] : ' ';
    }
    if (line == 1) {
        const uint l[14] = {'M','O','D','E',':',' ',' ',' ',' ',' ',' ',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.mode == 2) { const uint v2[15] = {'T','R','U','E',' ','F','G',' ','(','v','2',')',' ','>',' '}; return (col - 14 < 15) ? v2[col - 14] : ' '; }
        if (cpc.mode == 1) { const uint v1[13] = {'B','L','E','N','D',' ','(','v','1',')',' ','>',' '}; return (col - 14 < 13) ? v1[col - 14] : ' '; }
        const uint off[6] = {'O','F','F',' ','>',' '}; return (col - 14 < 6) ? off[col - 14] : ' ';
    }
    if (line == 2) {
        const uint l[14] = {'M','U','L','T','I','P','L','I','E','R',':',' ','<',' '};
        if (col < 14) return l[col];
        if (col == 14) return '0' + cpc.multiplier;
        if (col == 15) return 'X';
        if (col == 17) return '>';
        return ' ';
    }
    if (line == 3) {
        const uint l[14] = {'P','R','O','F','I','L','E',':',' ',' ',' ',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.profile == 1) { const uint q[12] = {'Q','U','A','L','I','T','Y',' ','>',' '}; return (col - 14 < 10) ? q[col - 14] : ' '; }
        const uint p[14] = {'P','E','R','F','O','R','M','A','N','C','E',' ','>'}; return (col - 14 < 13) ? p[col - 14] : ' ';
    }
    if (line == 4) {
        const uint l[14] = {'S','E','A','R','C','H',':',' ',' ',' ',' ',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.searchMode == 1) { const uint h[19] = {'H','I','G','H',' ','(','+','-','2','4','p','x',')',' ','>'}; return (col - 14 < 16) ? h[col - 14] : ' '; }
        const uint s[19] = {'S','T','A','N','D','A','R','D',' ','(','+','-','8','p','x',')',' ','>'}; return (col - 14 < 18) ? s[col - 14] : ' ';
    }
    if (line == 5) {
        const uint l[14] = {'S','C','H','E','D','U','L','E','R',':',' ',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.schedulerMode == 0) { const uint a[17] = {'A','U','T','O',' ','(','V','B','l','a','n','k',')',' ','>'}; return (col - 14 < 15) ? a[col - 14] : ' '; }
        if (cpc.schedulerMode == 1) { const uint f[8] = {'F','I','X','E','D',' ','>'}; return (col - 14 < 7) ? f[col - 14] : ' '; }
        const uint t[13] = {'T','A','R','G','E','T',' ','F','P','S',' ','>'}; return (col - 14 < 12) ? t[col - 14] : ' ';
    }
    if (line == 6) {
        const uint l[14] = {'T','A','R','G','E','T',' ','F','P','S',':',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.targetFps == 0) { const uint o[6] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? o[col - 14] : ' '; }
        uint f = cpc.targetFps;
        if (col == 14) return '0' + ((f / 100) % 10);
        if (col == 15) return '0' + ((f / 10) % 10);
        if (col == 16) return '0' + (f % 10);
        if (col == 18) return '>';
        return ' ';
    }
    if (line == 7) {
        const uint l[14] = {'F','A','L','L','B','A','C','K',':',' ',' ',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.fallbackAction == 2) { const uint d[13] = {'D','R','O','P',' ','(','V','R','R',')',' ','>'}; return (col - 14 < 12) ? d[col - 14] : ' '; }
        if (cpc.fallbackAction == 0) { const uint r[9] = {'R','E','P','E','A','T',' ','>'}; return (col - 14 < 8) ? r[col - 14] : ' '; }
        const uint b[8] = {'B','L','E','N','D',' ','>'}; return (col - 14 < 7) ? b[col - 14] : ' ';
    }
    if (line == 8) {
        const uint l[14] = {'H','U','D',' ','S','T','A','T','S',':',' ',' ','<',' '};
        if (col < 14) return l[col];
        if (cpc.isHudVisible != 0) { const uint o[5] = {'O','N',' ','>'}; return (col - 14 < 4) ? o[col - 14] : ' '; }
        const uint f[6] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? f[col - 14] : ' ';
    }
    if (line == 9) {
        const uint nav[36] = {'[','A','R','R','O','W','S',']',' ','N','A','V','I','G','A','T','E',' ',' ',' ','[','I','N','S','E','R','T',']',' ','C','L','O','S','E',' '};
        return (col < 35) ? nav[col] : ' ';
    }
    return ' ';
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolutionX || pixelCoord.y >= pc.resolutionY) return;

    if (pc.isMenuOpen == 0 && pc.isHudVisible == 0) return;

    float4 bg = targetFrame[pixelCoord];
    float4 themeColor = (pc.mode == 2) ? float4(0.0, 0.9, 1.0, 1.0) : float4(1.0, 0.7, 0.0, 1.0);
    if (pc.mode == 0) themeColor = float4(1.0, 0.2, 0.2, 1.0);

    // 1. Centered menu
    if (pc.isMenuOpen != 0) {
        uint menuW = 420;
        uint menuH = 210;
        uint menuX = (pc.resolutionX > menuW) ? ((pc.resolutionX - menuW) / 2) : 10;
        uint menuY = (pc.resolutionY > menuH) ? ((pc.resolutionY - menuH) / 2) : 10;

        if (pixelCoord.x >= menuX && pixelCoord.x < menuX + menuW &&
            pixelCoord.y >= menuY && pixelCoord.y < menuY + menuH) 
        {
            bool isBorder = (pixelCoord.x == menuX || pixelCoord.x == menuX + menuW - 1 ||
                             pixelCoord.y == menuY || pixelCoord.y == menuY + menuH - 1 ||
                             pixelCoord.y == menuY + 24 || pixelCoord.y == menuY + menuH - 24);
            if (isBorder) {
                targetFrame[pixelCoord] = themeColor;
                return;
            }

            uint relX = pixelCoord.x - (menuX + 16);
            uint relY = pixelCoord.y - (menuY + 6);
            uint lineIdx = relY / 18;
            uint lineY = relY % 18;
            uint charCol = relX / 8;
            uint charX = relX % 8;

            bool drawPixel = false;
            float4 textColor = float4(0.9, 0.95, 1.0, 1.0);

            if (lineIdx < 10 && lineY < 7 && charX < 5 && charCol < 44) {
                uint c = getCenteredMenuChar(lineIdx, charCol, pc);
                drawPixel = (getFontColumn(c, charX) & (1 << lineY)) != 0;

                if (lineIdx == pc.selectedItem + 1) {
                    textColor = float4(0.0, 1.0, 0.5, 1.0);
                } else if (lineIdx == 0) {
                    textColor = themeColor;
                }
            }

            if (drawPixel) {
                targetFrame[pixelCoord] = textColor;
            } else {
                targetFrame[pixelCoord] = lerp(bg, float4(0.02, 0.04, 0.06, 1.0), 0.94);
            }
            return;
        }
    }

    // 2. Corner HUD
    if (pc.isHudVisible != 0) {
        uint hudW = 200;
        uint hudH = 145;
        uint hudX = 20;
        uint hudY = 20;

        if (pc.hudCorner == 1 && pc.resolutionX > hudW + 20) hudX = pc.resolutionX - (hudW + 20);
        else if (pc.hudCorner == 2 && pc.resolutionY > hudH + 20) hudY = pc.resolutionY - (hudH + 20);
        else if (pc.hudCorner == 3 && pc.resolutionX > hudW + 20 && pc.resolutionY > hudH + 20) {
            hudX = pc.resolutionX - (hudW + 20);
            hudY = pc.resolutionY - (hudH + 20);
        }

        if (pixelCoord.x >= hudX && pixelCoord.x < hudX + hudW &&
            pixelCoord.y >= hudY && pixelCoord.y < hudY + hudH) 
        {
            bool isBorder = (pixelCoord.x == hudX || pixelCoord.x == hudX + hudW - 1 ||
                             pixelCoord.y == hudY || pixelCoord.y == hudY + hudH - 1 ||
                             pixelCoord.y == hudY + 18);
            if (isBorder) {
                targetFrame[pixelCoord] = themeColor;
                return;
            }

            uint relX = pixelCoord.x - (hudX + 8);
            uint relY = pixelCoord.y - (hudY + 5);
            uint lineIdx = relY / 13;
            uint lineY = relY % 13;
            uint charCol = relX / 7;
            uint charX = relX % 7;

            bool drawPixel = false;
            float4 textColor = float4(0.85, 0.9, 0.95, 1.0);

            if (lineIdx < 10 && lineY < 7 && charX < 5 && charCol < 20) {
                uint c = getHudChar(lineIdx, charCol, pc);
                drawPixel = (getFontColumn(c, charX) & (1 << lineY)) != 0;

                if (lineIdx == 0) textColor = themeColor;
                else if (lineIdx == 3) textColor = float4(0.0, 1.0, 0.5, 1.0);
                else if (charCol >= 9 && charCol < 15) textColor = float4(1.0, 0.85, 0.2, 1.0);
            }

            if (drawPixel) {
                targetFrame[pixelCoord] = textColor;
            } else {
                targetFrame[pixelCoord] = lerp(bg, float4(0.03, 0.05, 0.08, 1.0), 0.88);
            }
        }
    }
}