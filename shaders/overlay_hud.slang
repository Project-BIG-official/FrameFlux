// PB FrameFlux - LGPL-2.1
// overlay_hud.slang: Honest Diagnostics & Full 10-Item Debug Control Panel

struct OverlayPushConstants {
    uint resolutionX;       // Offset 0
    uint resolutionY;       // Offset 4
    uint isMenuOpen;        // Offset 8
    uint hudMode;           // Offset 12 
    uint selectedItem;      // Offset 16
    uint mode;              // Offset 20
    uint multiplier;        // Offset 24
    uint profile;           // Offset 28
    uint searchMode;        // Offset 32
    uint lowLatency;        // Offset 36 
    uint schedulerMode;     // Offset 40
    uint targetFps;         // Offset 44
    uint fallbackAction;    // Offset 48
    uint hudCorner;         // Offset 52
    uint nativeFpsX10;      // Offset 56
    uint outputFpsX10;      // Offset 60
    uint gpuTimeUs;         // Offset 64
    uint cpuTimeUs;         // Offset 68
    uint latencyUs;         // Offset 72
    uint confidenceX10;     // Offset 76
    uint fallbackX10;       // Offset 80
    uint vramMb;            // Offset 84
    uint displayHz;         // Offset 88
    uint isDebugMode;       // Offset 92

    // Advanced Debug Slots (Offsets 96..128 = 8 uints = 32 bytes)
    uint menuPage;          // Offset 96:  0 = Main, 1 = Debug
    uint extPresentWait;    // Offset 100: 0 = Auto, 1 = Wait2, 2 = Wait1, 3 = Disabled
    uint extLowLatency;     // Offset 104: 0 = Auto, 1 = AntiLag, 2 = Reflex, 3 = JIT, 4 = Off
    uint extDisplayTiming;  // Offset 108: 0 = Auto, 1 = EXT, 2 = Google, 3 = Sysfs, 4 = Fixed
    uint extTimestamps;     // Offset 112: 0 = Auto, 1 = KHR, 2 = EXT, 3 = QueryPool
    uint forceFallback;     // Offset 116: 0 = Auto, 1 = Repeat, 2 = Blend, 3 = Drop
    uint debugVisual;       // Offset 120: 0 = Off, 1 = Badge, 2 = Conf Map, 3 = Flow Vectors
    uint strideAndConf;     // Offset 124: byte 0 = Stride, byte 1 = Conf, byte 2 = SwapchainMaint
};

[[vk::push_constant]]
ConstantBuffer<OverlayPushConstants> pc;

[[vk::binding(0, 0)]] RWTexture2D<float4> targetFrame;

uint getFontColumn(uint c, uint col) {
    if (col >= 5) return 0;
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    switch (c) {
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

        case '.': { const uint d[5] = {0x00, 0x60, 0x60, 0x00, 0x00}; return d[col]; }
        case ':': { const uint d[5] = {0x00, 0x36, 0x36, 0x00, 0x00}; return d[col]; }
        case '%': { const uint d[5] = {0x23, 0x13, 0x08, 0x64, 0x62}; return d[col]; }
        case '-': { const uint d[5] = {0x08, 0x08, 0x08, 0x08, 0x08}; return d[col]; }
        case '+': { const uint d[5] = {0x08, 0x08, 0x3E, 0x08, 0x08}; return d[col]; }
        case '/': { const uint d[5] = {0x20, 0x10, 0x08, 0x04, 0x02}; return d[col]; }
        case '>': { const uint d[5] = {0x44, 0x28, 0x10, 0x00, 0x00}; return d[col]; }
        case '<': { const uint d[5] = {0x10, 0x28, 0x44, 0x00, 0x00}; return d[col]; }
        case '[': { const uint d[5] = {0x00, 0x7F, 0x41, 0x41, 0x00}; return d[col]; }
        case ']': { const uint d[5] = {0x00, 0x41, 0x41, 0x7F, 0x00}; return d[col]; }
        case '(': { const uint d[5] = {0x00, 0x1C, 0x22, 0x41, 0x00}; return d[col]; }
        case ')': { const uint d[5] = {0x00, 0x41, 0x22, 0x1C, 0x00}; return d[col]; }
        default:  return 0;
    }
}

uint getHudChar(uint line, uint col, OverlayPushConstants cpc) {
    if (cpc.hudMode == 1) {
        if (line == 1) {
            const uint l[5] = {'F','P','S',':',' '};
            if (col < 5) return l[col];
            uint val = cpc.outputFpsX10;
            if (col == 5) return '0' + ((val / 1000) % 10);
            if (col == 6) return '0' + ((val / 100) % 10);
            if (col == 7) return '0' + ((val / 10) % 10);
            if (col == 8) return '.';
            if (col == 9) return '0' + (val % 10);
        }
        if (line == 2) {
            const uint l[5] = {'G','P','U',':',' '};
            if (col < 5) return l[col];
            uint val = (cpc.gpuTimeUs + 50) / 100;
            if (col == 5) return '0' + ((val / 10) % 10);
            if (col == 6) return '.';
            if (col == 7) return '0' + (val % 10);
            if (col == 8) return 'M';
            if (col == 9) return 'S';
        }
        return ' ';
    }

    if (cpc.hudMode == 3) {
        if (line == 0) { const uint t[13] = {'L','A','T','E','N','C','Y',' ','S','T','A','T','S'}; return (col < 13) ? t[col] : ' '; }
        if (col < 9) {
            if (line == 1) { const uint l[9] = {'N','A','T','I','V','E',':',' ',' '}; return l[col]; }
            if (line == 2) { const uint l[9] = {'O','U','T','P','U','T',':',' ',' '}; return l[col]; }
            if (line == 3) { const uint l[9] = {'L','A','T','E','N','C','Y',':',' '}; return l[col]; }
            if (line == 4) { const uint l[9] = {'L','L',' ','P','R','O','V',':',' '}; return l[col]; }
            if (line == 5) { const uint l[9] = {'D','I','S','P',' ','H','Z',':',' '}; return l[col]; }
            return ' ';
        }
        if (col >= 9 && col < 18) {
            uint val = 0;
            if (line == 1) val = cpc.nativeFpsX10;
            else if (line == 2) val = cpc.outputFpsX10;
            else if (line == 3) val = (cpc.latencyUs + 50) / 100;
            else if (line == 5) val = cpc.displayHz * 10;

            if (line == 4) {
                if (cpc.lowLatency == 0) { const uint off[3] = {'O','F','F'}; return (col - 9 < 3) ? off[col - 9] : ' '; }
                if (cpc.extLowLatency == 2) { const uint rx[6] = {'R','E','F','L','E','X'}; return (col - 9 < 6) ? rx[col - 9] : ' '; }
                if (cpc.extLowLatency == 1) { const uint al[7] = {'A','N','T','I','L','A','G'}; return (col - 9 < 7) ? al[col - 9] : ' '; }
                const uint jit[8] = {'J','I','T',' ','E','M','U','L'}; return (col - 9 < 8) ? jit[col - 9] : ' ';
            }

            if (line == 1 || line == 2 || line == 3 || line == 5) {
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
            }
        }
        return ' ';
    }

    if (line == 0) { const uint t[13] = {'F','R','A','M','E','F','L','U','X',' ','2','.','1'}; return (col < 13) ? t[col] : ' '; }
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
            case 4: val = (cpc.gpuTimeUs + 50) / 100; break;
            case 5: val = (cpc.cpuTimeUs + 50) / 100; break;
            case 6: val = (cpc.latencyUs + 50) / 100; break;
            case 7: val = cpc.confidenceX10; break;
            case 8: val = cpc.fallbackX10; break;
            case 9: val = cpc.vramMb * 10; break;
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
    if (cpc.menuPage == 0) {
        if (line == 0) {
            const uint h[25] = {'[','-','-',' ','P','B',' ','F','R','A','M','E','F','L','U','X',' ','M','E','N','U',' ','-','-',']'};
            return (col < 25) ? h[col] : ' ';
        }
        if (line == 1) {
            const uint l[14] = {'M','O','D','E',':',' ',' ',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.mode == 2) { const uint v2[15] = {'T','R','U','E',' ','F','G',' ','(','v','2',')',' ','>',' '}; return (col - 14 < 15) ? v2[col - 14] : ' '; }
            if (cpc.mode == 1) { const uint v1[13] = {'B','L','E','N','D',' ','(','v','1',')',' ','>',' '}; return (col - 14 < 13) ? v1[col - 14] : ' '; }
            const uint off[6] = {'O','F','F',' ','>',' '}; return (col - 14 < 6) ? off[col - 14] : ' ';
        }
        if (line == 2) {
            const uint l[14] = {'T','A','R','G','E','T',' ','F','P','S',':',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.targetFps == 0) { const uint o[5] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? o[col - 14] : ' '; }
            uint f = cpc.targetFps;
            uint d0 = (f / 100) % 10;
            if (col == 14) return (d0 > 0) ? ('0' + d0) : ' ';
            if (col == 15) return '0' + ((f / 10) % 10);
            if (col == 16) return '0' + (f % 10);
            if (col == 18) return '>';
            return ' ';
        }
        if (line == 3) {
            const uint l[14] = {'M','U','L','T','I','P','L','I','E','R',':',' ','<',' '};
            if (col < 14) return l[col];
            if (col == 14) return '0' + cpc.multiplier;
            if (col == 15) return 'X';
            if (col == 17) return '>';
            return ' ';
        }
        if (line == 4) {
            const uint l[14] = {'S','C','H','E','D','U','L','E','R',':',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.schedulerMode == 2) { const uint tf[12] = {'T','A','R','G','E','T',' ','F','P','S',' ','>'}; return (col - 14 < 12) ? tf[col - 14] : ' '; }
            if (cpc.schedulerMode == 1) { const uint fx[7] = {'F','I','X','E','D',' ','>'}; return (col - 14 < 7) ? fx[col - 14] : ' '; }
            const uint au[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? au[col - 14] : ' ';
        }
        if (line == 5) {
            const uint l[14] = {'L','O','W',' ','L','A','T','E','N','C','Y',':','<',' '};
            if (col < 14) return l[col];
            if (cpc.lowLatency == 2) { const uint b[7] = {'B','O','O','S','T',' ','>'}; return (col - 14 < 7) ? b[col - 14] : ' '; }
            if (cpc.lowLatency == 1) { const uint o[5] = {'O','N',' ','>',' '}; return (col - 14 < 5) ? o[col - 14] : ' '; }
            const uint f[5] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? f[col - 14] : ' ';
        }
        if (line == 6) {
            const uint l[14] = {'P','R','O','F','I','L','E',':',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.profile == 1) { const uint q[10] = {'Q','U','A','L','I','T','Y',' ','>',' '}; return (col - 14 < 10) ? q[col - 14] : ' '; }
            const uint p[13] = {'P','E','R','F','O','R','M','A','N','C','E',' ','>'}; return (col - 14 < 13) ? p[col - 14] : ' ';
        }
        if (line == 7) {
            const uint l[14] = {'S','E','A','R','C','H',':',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.searchMode == 1) { const uint h[16] = {'H','I','G','H',' ','(','+','-','2','4','P','X',')',' ','>',' '}; return (col - 14 < 16) ? h[col - 14] : ' '; }
            const uint s[18] = {'S','T','A','N','D','A','R','D',' ','(','+','-','8','P','X',')',' ','>'}; return (col - 14 < 18) ? s[col - 14] : ' ';
        }
        if (line == 8) {
            const uint l[14] = {'F','A','L','L','B','A','C','K',':',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.fallbackAction == 2) { const uint d[12] = {'D','R','O','P',' ','(','V','R','R',')',' ','>'}; return (col - 14 < 12) ? d[col - 14] : ' '; }
            if (cpc.fallbackAction == 0) { const uint r[8] = {'R','E','P','E','A','T',' ','>'}; return (col - 14 < 8) ? r[col - 14] : ' '; }
            const uint b[7] = {'B','L','E','N','D',' ','>'}; return (col - 14 < 7) ? b[col - 14] : ' ';
        }
        if (line == 9) {
            const uint l[14] = {'H','U','D',' ','M','O','D','E',':',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.hudMode == 3) { const uint lt[9] = {'L','A','T','E','N','C','Y',' ','>'}; return (col - 14 < 9) ? lt[col - 14] : ' '; }
            if (cpc.hudMode == 2) { const uint f[6] = {'F','U','L','L',' ','>'}; return (col - 14 < 6) ? f[col - 14] : ' '; }
            if (cpc.hudMode == 1) { const uint c[9] = {'C','O','M','P','A','C','T',' ','>'}; return (col - 14 < 9) ? c[col - 14] : ' '; }
            const uint o[5] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? o[col - 14] : ' ';
        }
        if (line == 10) {
            const uint l[14] = {'H','U','D',' ','C','O','R','N','E','R',':',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.hudCorner == 0) { const uint tl[10] = {'T','O','P','-','L','E','F','T',' ','>'}; return (col - 14 < 10) ? tl[col - 14] : ' '; }
            if (cpc.hudCorner == 1) { const uint tr[11] = {'T','O','P','-','R','I','G','H','T',' ','>'}; return (col - 14 < 11) ? tr[col - 14] : ' '; }
            if (cpc.hudCorner == 2) { const uint bl[13] = {'B','O','T','T','O','M','-','L','E','F','T',' ','>'}; return (col - 14 < 13) ? bl[col - 14] : ' '; }
            if (cpc.hudCorner == 3) { const uint br[14] = {'B','O','T','T','O','M','-','R','I','G','H','T',' ','>'}; return (col - 14 < 14) ? br[col - 14] : ' '; }
        }
        if (line == 11) {
            const uint nav[34] = {'[','A','R','R','O','W','S',']',' ','A','D','J','U','S','T',' ',' ','[','T','A','B',']',' ','D','E','B','U','G',' ','P','A','G','E',' '};
            return (col < 34) ? nav[col] : ' ';
        }
    } else {
        uint strideCode = cpc.strideAndConf & 0xFF;
        uint confCode   = (cpc.strideAndConf >> 8) & 0xFF;
        uint maintCode  = (cpc.strideAndConf >> 16) & 0xFF;

        if (line == 0) {
            const uint h[23] = {'[','-','-',' ','P','B',' ','D','E','B','U','G',' ','P','A','N','E','L',' ','-','-',']',' '};
            return (col < 23) ? h[col] : ' ';
        }
        if (line == 1) {
            const uint l[14] = {'P','R','E','S','E','N','T',':',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.extPresentWait == 1) { const uint w2[13] = {'W','A','I','T','2',' ','(','K','H','R',')',' ','>'}; return (col - 14 < 13) ? w2[col - 14] : ' '; }
            if (cpc.extPresentWait == 2) { const uint w1[13] = {'W','A','I','T','1',' ','(','K','H','R',')',' ','>'}; return (col - 14 < 13) ? w1[col - 14] : ' '; }
            if (cpc.extPresentWait == 3) { const uint dis[10] = {'D','I','S','A','B','L','E','D',' ','>'}; return (col - 14 < 10) ? dis[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 2) {
            const uint l[14] = {'L','A','T','E','N','C','Y',':',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.extLowLatency == 1) { const uint al[16] = {'A','N','T','I','-','L','A','G',' ','(','A','M','D',')',' ','>'}; return (col - 14 < 16) ? al[col - 14] : ' '; }
            if (cpc.extLowLatency == 2) { const uint rx[13] = {'R','E','F','L','E','X',' ','(','N','V',')',' ','>'}; return (col - 14 < 13) ? rx[col - 14] : ' '; }
            if (cpc.extLowLatency == 3) { const uint jit[14] = {'J','I','T',' ','E','M','U','L','A','T','E','D',' ','>'}; return (col - 14 < 14) ? jit[col - 14] : ' '; }
            if (cpc.extLowLatency == 4) { const uint off[5] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? off[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 3) {
            const uint l[14] = {'T','I','M','I','N','G',':',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.extDisplayTiming == 1) { const uint et[12] = {'E','X','T',' ','T','I','M','I','N','G',' ','>'}; return (col - 14 < 12) ? et[col - 14] : ' '; }
            if (cpc.extDisplayTiming == 2) { const uint gt[15] = {'G','O','O','G','L','E',' ','T','I','M','I','N','G',' ','>'}; return (col - 14 < 15) ? gt[col - 14] : ' '; }
            if (cpc.extDisplayTiming == 3) { const uint ds[11] = {'D','R','M',' ','S','Y','S','F','S',' ','>'}; return (col - 14 < 11) ? ds[col - 14] : ' '; }
            if (cpc.extDisplayTiming == 4) { const uint fx[13] = {'F','I','X','E','D',' ','1','6','5','H','Z',' ','>'}; return (col - 14 < 13) ? fx[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 4) {
            const uint l[14] = {'C','L','O','C','K','S',':',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.extTimestamps == 1) { const uint ck[16] = {'C','A','L','I','B','R','A','T','E','D',' ','K','H','R',' ','>'}; return (col - 14 < 16) ? ck[col - 14] : ' '; }
            if (cpc.extTimestamps == 2) { const uint ce[16] = {'C','A','L','I','B','R','A','T','E','D',' ','E','X','T',' ','>'}; return (col - 14 < 16) ? ce[col - 14] : ' '; }
            if (cpc.extTimestamps == 3) { const uint qp[15] = {'Q','U','E','R','Y','P','O','O','L',' ','G','P','U',' ','>'}; return (col - 14 < 15) ? qp[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 5) {
            const uint l[14] = {'S','W','A','P','C','H','A','I','N',':',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (maintCode == 1) { const uint m1[14] = {'M','A','I','N','T','1',' ','(','K','H','R',')',' ','>'}; return (col - 14 < 14) ? m1[col - 14] : ' '; }
            if (maintCode == 2) { const uint sf[17] = {'S','T','A','N','D','A','R','D',' ','F','E','N','C','E','S',' ','>'}; return (col - 14 < 17) ? sf[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 6) {
            const uint l[14] = {'F','O','R','C','E',' ','F','G',':',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.forceFallback == 1) { const uint r[14] = {'F','O','R','C','E',' ','R','E','P','E','A','T',' ','>'}; return (col - 14 < 14) ? r[col - 14] : ' '; }
            if (cpc.forceFallback == 2) { const uint b[13] = {'F','O','R','C','E',' ','B','L','E','N','D',' ','>'}; return (col - 14 < 13) ? b[col - 14] : ' '; }
            if (cpc.forceFallback == 3) { const uint d[12] = {'F','O','R','C','E',' ','D','R','O','P',' ','>'}; return (col - 14 < 12) ? d[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 7) {
            const uint l[14] = {'C','O','N','F',':',' ',' ',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (confCode == 1) { const uint c1[12] = {'0','.','2','0',' ','(','M','I','N',')',' ','>'}; return (col - 14 < 12) ? c1[col - 14] : ' '; }
            if (confCode == 2) { const uint c2[12] = {'0','.','4','0',' ','(','L','O','W',')',' ','>'}; return (col - 14 < 12) ? c2[col - 14] : ' '; }
            if (confCode == 3) { const uint c3[12] = {'0','.','6','0',' ','(','M','I','D',')',' ','>'}; return (col - 14 < 12) ? c3[col - 14] : ' '; }
            if (confCode == 4) { const uint c4[13] = {'0','.','8','0',' ','(','H','I','G','H',')',' ','>'}; return (col - 14 < 13) ? c4[col - 14] : ' '; }
            if (confCode == 5) { const uint c5[12] = {'0','.','9','5',' ','(','M','A','X',')',' ','>'}; return (col - 14 < 12) ? c5[col - 14] : ' '; }
            const uint a[13] = {'A','U','T','O',' ','(','0','.','5','5',')',' ','>'}; return (col - 14 < 13) ? a[col - 14] : ' ';
        }
        if (line == 8) {
            const uint l[14] = {'S','T','R','I','D','E',':',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (strideCode == 1) { const uint s1[12] = {'1','P','X',' ','(','S','L','O','W',')',' ','>'}; return (col - 14 < 12) ? s1[col - 14] : ' '; }
            if (strideCode == 2) { const uint s2[16] = {'2','P','X',' ','(','B','A','L','A','N','C','E','D',')',' ','>'}; return (col - 14 < 16) ? s2[col - 14] : ' '; }
            if (strideCode == 3) { const uint s3[12] = {'3','P','X',' ','(','F','A','S','T',')',' ','>'}; return (col - 14 < 12) ? s3[col - 14] : ' '; }
            const uint a[6] = {'A','U','T','O',' ','>'}; return (col - 14 < 6) ? a[col - 14] : ' ';
        }
        if (line == 9) {
            const uint l[14] = {'V','I','S','U','A','L',':',' ',' ',' ',' ',' ','<',' '};
            if (col < 14) return l[col];
            if (cpc.debugVisual == 1) { const uint bg[12] = {'[','F','G',']',' ','B','A','D','G','E',' ','>'}; return (col - 14 < 12) ? bg[col - 14] : ' '; }
            if (cpc.debugVisual == 2) { const uint cm[11] = {'C','O','N','F',' ','M','A','S','K',' ','>'}; return (col - 14 < 11) ? cm[col - 14] : ' '; }
            if (cpc.debugVisual == 3) { const uint fv[14] = {'F','L','O','W',' ','V','E','C','T','O','R','S',' ','>'}; return (col - 14 < 14) ? fv[col - 14] : ' '; }
            const uint o[5] = {'O','F','F',' ','>'}; return (col - 14 < 5) ? o[col - 14] : ' ';
        }
        if (line == 10) {
            const uint sw[27] = {'S','W','I','T','C','H',':',' ','<',' ','[','T','A','B',']',' ','M','A','I','N',' ','P','A','G','E',' ','>'};
            return (col < 27) ? sw[col] : ' ';
        }
        if (line == 11) {
            const uint nav[33] = {'[','A','R','R','O','W','S',']',' ','A','D','J','U','S','T',' ',' ','[','T','A','B',']',' ','M','A','I','N',' ','P','A','G','E'};
            return (col < 33) ? nav[col] : ' ';
        }
    }
    return ' ';
}

[shader("compute")]
[numthreads(16, 16, 1)]
[WaveSize(32)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= pc.resolutionX || pixelCoord.y >= pc.resolutionY) return;

    if (pc.debugVisual == 1) {
        uint badgeX = pc.resolutionX - 52;
        uint badgeY = 16;
        if (pixelCoord.x >= badgeX && pixelCoord.x < badgeX + 36 &&
            pixelCoord.y >= badgeY && pixelCoord.y < badgeY + 16) 
        {
            uint bx = pixelCoord.x - badgeX;
            uint by = pixelCoord.y - badgeY;
            bool isBorder = (bx == 0 || bx == 35 || by == 0 || by == 15);
            if (isBorder) {
                targetFrame[pixelCoord] = float4(0.0, 1.0, 0.5, 1.0);
                return;
            }
            if (by >= 4 && by < 11 && bx >= 8 && bx < 28) {
                uint col = (bx - 8) / 5;
                uint cx = (bx - 8) % 5;
                uint cy = by - 4;
                uint ch = (col == 0) ? '[' : ((col == 1) ? 'F' : ((col == 2) ? 'G' : ']'));
                if ((getFontColumn(ch, cx) & (1 << cy)) != 0) {
                    targetFrame[pixelCoord] = float4(1.0, 1.0, 1.0, 1.0);
                    return;
                }
            }
            targetFrame[pixelCoord] = float4(0.02, 0.15, 0.08, 0.85);
            return;
        }
    }

    if (pc.isMenuOpen == 0 && pc.hudMode == 0) return;

    float4 bg = targetFrame[pixelCoord];
    float4 themeColor = (pc.mode == 2) ? float4(0.0, 0.9, 1.0, 1.0) : float4(1.0, 0.7, 0.0, 1.0);
    if (pc.mode == 0) themeColor = float4(1.0, 0.2, 0.2, 1.0);
    if (pc.menuPage == 1) themeColor = float4(1.0, 0.5, 0.1, 1.0);

    if (pc.isMenuOpen != 0) {
        uint menuW = 430;
        uint menuH = 236;
        uint menuX = (pc.resolutionX > menuW) ? ((pc.resolutionX - menuW) / 2) : 10;
        uint menuY = (pc.resolutionY > menuH) ? ((pc.resolutionY - menuH) / 2) : 10;

        if (pixelCoord.x >= menuX && pixelCoord.x < menuX + menuW &&
            pixelCoord.y >= menuY && pixelCoord.y < menuY + menuH) 
        {
            bool isOuterBorder = (pixelCoord.x == menuX || pixelCoord.x == menuX + menuW - 1 ||
                                  pixelCoord.y == menuY || pixelCoord.y == menuY + menuH - 1);
            bool isTopDivider = (pixelCoord.y == menuY + 23);
            bool isBottomDivider = (pixelCoord.y == menuY + menuH - 24);

            if (isOuterBorder || isTopDivider || isBottomDivider) {
                targetFrame[pixelCoord] = themeColor;
                return;
            }

            uint localX = pixelCoord.x - menuX;
            uint localY = pixelCoord.y - menuY;

            uint lineIdx = 99;
            uint lineY = 0;

            if (localY >= 8 && localY < 15) {
                lineIdx = 0;
                lineY = localY - 8;
            } else if (localY >= 31 && localY < 201) {
                uint itemRelY = localY - 31;
                uint itemIdx = itemRelY / 17;
                lineY = itemRelY % 17;
                if (lineY < 7 && itemIdx < 10) {
                    lineIdx = itemIdx + 1;
                }
            } else if (localY >= menuH - 17 && localY < menuH - 10) {
                lineIdx = 11;
                lineY = localY - (menuH - 17);
            }

            if (lineIdx != 99 && localX >= 16) {
                uint textRelX = localX - 16;
                uint charCol = textRelX / 8;
                uint charX = textRelX % 8;

                if (charX < 5 && charCol < 48) {
                    uint c = getCenteredMenuChar(lineIdx, charCol, pc);
                    if ((getFontColumn(c, charX) & (1 << lineY)) != 0) {
                        float4 textColor = float4(0.9, 0.95, 1.0, 1.0);
                        if (lineIdx == pc.selectedItem + 1) {
                            textColor = (pc.menuPage == 1) ? float4(1.0, 0.85, 0.2, 1.0) : float4(0.0, 1.0, 0.5, 1.0);
                        } else if (lineIdx == 0) {
                            textColor = themeColor;
                        } else if (lineIdx == 11) {
                            textColor = float4(0.6, 0.7, 0.8, 1.0);
                        }
                        targetFrame[pixelCoord] = textColor;
                        return;
                    }
                }
            }

            targetFrame[pixelCoord] = lerp(bg, float4(0.02, 0.04, 0.06, 1.0), 0.94);
            return;
        }
    }

    if (pc.hudMode != 0) {
        uint hudW = (pc.hudMode == 1) ? 120 : ((pc.hudMode == 3) ? 175 : 190);
        uint hudH = (pc.hudMode == 1) ? 46  : ((pc.hudMode == 3) ? 116 : 160);
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
            bool isOuterBorder = (pixelCoord.x == hudX || pixelCoord.x == hudX + hudW - 1 ||
                                  pixelCoord.y == hudY || pixelCoord.y == hudY + hudH - 1);
            bool isHeaderDivider = (pc.hudMode != 1) ? (pixelCoord.y == hudY + 19) : false;

            if (isOuterBorder || isHeaderDivider) {
                targetFrame[pixelCoord] = themeColor;
                return;
            }

            uint localX = pixelCoord.x - hudX;
            uint localY = pixelCoord.y - hudY;

            uint lineIdx = 99;
            uint lineY = 0;

            if (pc.hudMode == 1) {
                if (localY >= 8 && localY < 21) {
                    lineIdx = 1;
                    lineY = (localY - 8) % 13;
                } else if (localY >= 23 && localY < 36) {
                    lineIdx = 2;
                    lineY = (localY - 23) % 13;
                }
            } else {
                if (localY >= 6 && localY < 13) {
                    lineIdx = 0;
                    lineY = localY - 6;
                } else if (localY >= 25) {
                    uint bodyRelY = localY - 25;
                    uint row = bodyRelY / 14;
                    lineY = bodyRelY % 14;

                    uint maxRows = (pc.hudMode == 3) ? 5 : 9;
                    if (lineY < 7 && row < maxRows) {
                        lineIdx = row + 1;
                    }
                }
            }

            if (lineIdx != 99 && localX >= 8 && lineY < 7) {
                uint textRelX = localX - 8;
                uint charCol = textRelX / 7;
                uint charX = textRelX % 7;

                if (charX < 5 && charCol < 22) {
                    uint c = getHudChar(lineIdx, charCol, pc);
                    if ((getFontColumn(c, charX) & (1 << lineY)) != 0) {
                        float4 textColor = float4(0.85, 0.9, 0.95, 1.0);
                        if (lineIdx == 0) {
                            textColor = themeColor;
                        } else if (charCol >= 5 && pc.hudMode == 1) {
                            textColor = float4(1.0, 0.85, 0.2, 1.0);
                        } else if (charCol >= 9 && charCol < 15) {
                            textColor = float4(1.0, 0.85, 0.2, 1.0);
                        }
                        targetFrame[pixelCoord] = textColor;
                        return;
                    }
                }
            }

            targetFrame[pixelCoord] = lerp(bg, float4(0.03, 0.05, 0.08, 1.0), 0.88);
        }
    }
}