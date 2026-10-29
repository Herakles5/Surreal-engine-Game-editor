#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - HIGH-TECH HUD, TELEMETRY & LIVE BENCHMARK SYSTEM
 * Renders real-time FPS, Frame Times, Meshlet Culling, and Stress Benchmarks
 * Directly into the Framebuffer with Zero External Dependencies
 * ============================================================================
 */

#include <stdint.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace Surreal {

// Embedded 8x8 Clean Monospace Cyber Font Bitmaps (ASCII 32..126)
static const uint8_t CYBER_FONT_8X8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // 35 '#'
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // 36 '$'
    {0x00,0x66,0xA6,0xD4,0x2B,0x65,0x66,0x00}, // 37 '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // 38 '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // 39 '''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // 40 '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // 41 ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // 47 '/'
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, // 48 '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 49 '1'
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}, // 50 '2'
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // 51 '3'
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, // 52 '4'
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // 53 '5'
    {0x78,0x0C,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // 54 '6'
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, // 55 '7'
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // 56 '8'
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // 57 '9'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // 58 ':'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // 59 ';'
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 60 '<'
    {0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00}, // 61 '='
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // 62 '>'
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, // 63 '?'
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x7C,0x00}, // 64 '@'
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 65 'A'
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 66 'B'
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 67 'C'
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 68 'D'
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 69 'E'
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, // 70 'F'
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, // 71 'G'
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 72 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 73 'I'
    {0x1E,0x0C,0x0C,0x0C,0x0C,0xCC,0x78,0x00}, // 74 'J'
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // 75 'K'
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, // 76 'L'
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, // 77 'M'
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 78 'N'
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 79 'O'
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 80 'P'
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06}, // 81 'Q'
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, // 82 'R'
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, // 83 'S'
    {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00}, // 84 'T'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 85 'U'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 86 'V'
    {0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0xC6,0x00}, // 87 'W'
    {0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00}, // 88 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 89 'Y'
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, // 90 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // 91 '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // 92 '\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // 93 ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 97 'a'
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, // 98 'b'
    {0x00,0x00,0x78,0xCC,0xC0,0xCC,0x78,0x00}, // 99 'c'
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, // 100 'd'
    {0x00,0x00,0x78,0xCC,0xFC,0xC0,0x78,0x00}, // 101 'e'
    {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00}, // 102 'f'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, // 103 'g'
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 104 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 105 'i'
    {0x0C,0x00,0x1C,0x0C,0x0C,0xCC,0xCC,0x78}, // 106 'j'
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 107 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 108 'l'
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00}, // 109 'm'
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 110 'n'
    {0x00,0x00,0x78,0xCC,0xCC,0xCC,0x78,0x00}, // 111 'o'
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, // 112 'p'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, // 113 'q'
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, // 114 'r'
    {0x00,0x00,0x7C,0xC0,0x78,0x0C,0xF8,0x00}, // 115 's'
    {0x10,0x30,0x7C,0x30,0x30,0x34,0x18,0x00}, // 116 't'
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 117 'u'
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 118 'v'
    {0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00}, // 119 'w'
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 120 'x'
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC}, // 121 'y'
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00}, // 122 'z'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // 125 '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  // 126 '~'
};

struct BenchmarkResults {
    bool isCompleted;
    int totalFrames;
    float minFps;
    float maxFps;
    float avgFps;
    float avgFrameTimeMs;
    float avgSkinningMs;
    float avgRenderMs;
    float meshletCullingEfficiency;
    uint64_t totalVertices;
    uint64_t totalTriangles;

    BenchmarkResults()
        : isCompleted(false), totalFrames(0), minFps(0), maxFps(0), avgFps(0),
          avgFrameTimeMs(0), avgSkinningMs(0), avgRenderMs(0),
          meshletCullingEfficiency(0), totalVertices(0), totalTriangles(0) {}
};

class SurrealHUD {
public:
    static void DrawChar(uint32_t* fb, int screenW, int screenH, int x, int y, char c, uint32_t color, int scale = 1) {
        if (c < 32 || c > 126) c = '?';
        const uint8_t* glyph = CYBER_FONT_8X8[c - 32];

        for (int row = 0; row < 8; ++row) {
            uint8_t rowBits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (rowBits & (0x80 >> col)) {
                    for (int dy = 0; dy < scale; ++dy) {
                        int py = y + row * scale + dy;
                        if (py < 0 || py >= screenH) continue;
                        for (int dx = 0; dx < scale; ++dx) {
                            int px = x + col * scale + dx;
                            if (px < 0 || px >= screenW) continue;
                            fb[py * screenW + px] = color;
                        }
                    }
                }
            }
        }
    }

    static void DrawString(uint32_t* fb, int screenW, int screenH, int x, int y, const std::string& text, uint32_t color, int scale = 1) {
        int curX = x;
        for (char c : text) {
            DrawChar(fb, screenW, screenH, curX, y, c, color, scale);
            curX += 8 * scale;
        }
    }

    static void DrawPanel(uint32_t* fb, int screenW, int screenH, int x, int y, int w, int h, uint32_t color, float alpha) {
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;

        int minX = std::max(0, x);
        int maxX = std::min(screenW - 1, x + w);
        int minY = std::max(0, y);
        int maxY = std::min(screenH - 1, y + h);

        float invAlpha = 1.0f - alpha;

        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                uint32_t bg = fb[py * screenW + px];
                int bgR = (bg >> 16) & 0xFF;
                int bgG = (bg >> 8) & 0xFF;
                int bgB = bg & 0xFF;

                int outR = static_cast<int>(r * alpha + bgR * invAlpha);
                int outG = static_cast<int>(g * alpha + bgG * invAlpha);
                int outB = static_cast<int>(b * alpha + bgB * invAlpha);

                fb[py * screenW + px] = (0xFF << 24) | (outR << 16) | (outG << 8) | outB;
            }
        }

        // Cyan Border Ring
        uint32_t borderColor = 0xFF00FFCC;
        for (int px = minX; px <= maxX; ++px) {
            fb[minY * screenW + px] = borderColor;
            fb[maxY * screenW + px] = borderColor;
        }
        for (int py = minY; py <= maxY; ++py) {
            fb[py * screenW + minX] = borderColor;
            fb[py * screenW + maxX] = borderColor;
        }
    }

    static void DrawProgressBar(uint32_t* fb, int screenW, int screenH, int x, int y, int w, int h, float percent, uint32_t fgColor, uint32_t bgColor) {
        percent = std::max(0.0f, std::min(1.0f, percent));
        int fillW = static_cast<int>(w * percent);

        for (int py = y; py < y + h; ++py) {
            if (py < 0 || py >= screenH) continue;
            for (int px = x; px < x + w; ++px) {
                if (px < 0 || px >= screenW) continue;
                fb[py * screenW + px] = (px < x + fillW) ? fgColor : bgColor;
            }
        }
    }

    /**
     * Render Full Interactive HUD Overlay
     */
    static void RenderHUD(
        uint32_t* fb, int screenW, int screenH,
        float currentFps, float frameTimeMs, float skinningMs,
        uint64_t renderedMeshlets, uint64_t culledMeshlets,
        uint64_t totalVertices, uint64_t totalTriangles,
        const std::string& animClipName, float animTime,
        float camDist, float camPitch, float camYaw,
        bool isBenchmarkRunning, int benchmarkProgressPercent,
        const BenchmarkResults* benchResults = nullptr)
    {
        // 1. Primary Telemetry Box (Top-Left) positioned cleanly under top bar
        int panelX = 12;
        int panelY = 56;
        int panelW = 380;
        int panelH = 230;

        DrawPanel(fb, screenW, screenH, panelX, panelY, panelW, panelH, 0x070B16, 0.88f);

        // Header Title
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 12, "AION SURREAL 3D ENGINE // LIVE DOJO", 0xFF00FFCC, 1);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 24, "------------------------------------", 0xFF336688, 1);

        // FPS with color-coding
        char buf[128];
        uint32_t fpsColor = (currentFps >= 55.0f) ? 0xFF00FF88 : ((currentFps >= 28.0f) ? 0xFFFFCC00 : 0xFFFF3366);
        std::snprintf(buf, sizeof(buf), "FPS: %.1f  (%.2f ms/frame)", currentFps, frameTimeMs);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 38, buf, fpsColor, 1);

        // Sub-systems Latency
        std::snprintf(buf, sizeof(buf), "CPU Skinning (61 Bones): %.2f ms", skinningMs);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 54, buf, 0xFFE0E0E0, 1);

        std::snprintf(buf, sizeof(buf), "Render & PBR Shading:    %.2f ms", std::max(0.0f, frameTimeMs - skinningMs));
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 68, buf, 0xFFE0E0E0, 1);

        // Geometry & Meshlets
        std::snprintf(buf, sizeof(buf), "Model Verts: %lu | Tris: %lu", (unsigned long)totalVertices, (unsigned long)totalTriangles);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 86, buf, 0xFF99BBEE, 1);

        uint64_t totalMeshlets = renderedMeshlets + culledMeshlets;
        float cullingEfficiency = totalMeshlets > 0 ? (culledMeshlets * 100.0f / totalMeshlets) : 0.0f;
        std::snprintf(buf, sizeof(buf), "Meshlets: %lu Act. | %lu Culled (%.1f%%)",
                      (unsigned long)renderedMeshlets, (unsigned long)culledMeshlets, cullingEfficiency);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 100, buf, 0xFFFF007F, 1);

        // Camera Telemetry
        std::snprintf(buf, sizeof(buf), "Cam: Dist=%.1fm | P=%.2f | Y=%.2f", camDist, camPitch, camYaw);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 118, buf, 0xFF00E5FF, 1);

        // Animation Status
        std::snprintf(buf, sizeof(buf), "Animation: %s [%.2fs]", animClipName.c_str(), animTime);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 132, buf, 0xFFFFAA00, 1);

        // AION Soul Invariant
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 150, "AION MIND: Gen 1285+ [SOVEREIGN ACTIVE]", 0xFF00FFCC, 1);

        // Controls hint
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 172, "[SPACE] Jump | [WASD] Run | [Q/E] Cam Orbit", 0xFF00FFCC, 1);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 186, "[L-DRAG] Free Cam | [WHEEL/Z/X] Zoom (1.2-6m)", 0xFF8899AA, 1);
        DrawString(fb, screenW, screenH, panelX + 12, panelY + 200, "[B] Benchmark | [V/R] Reset Cam | [P] Pause", 0xFF8899AA, 1);

        // 2. Active Benchmark Progress Banner
        if (isBenchmarkRunning) {
            DrawPanel(fb, screenW, screenH, screenW / 2 - 180, 20, 360, 60, 0x1A0822, 0.95f);
            std::snprintf(buf, sizeof(buf), "BENCHMARK STRESS TEST RUNNING... %d%%", benchmarkProgressPercent);
            DrawString(fb, screenW, screenH, screenW / 2 - 160, 32, buf, 0xFFFF007F, 1);
            DrawProgressBar(fb, screenW, screenH, screenW / 2 - 160, 52, 320, 12,
                            benchmarkProgressPercent / 100.0f, 0xFF00FFCC, 0xFF223344);
        }

        // 3. Completed Benchmark Results Card (Center Overlay)
        if (benchResults && benchResults->isCompleted) {
            int bw = 460;
            int bh = 250;
            int bx = (screenW - bw) / 2;
            int by = (screenH - bh) / 2;

            DrawPanel(fb, screenW, screenH, bx, by, bw, bh, 0x050812, 0.96f);

            DrawString(fb, screenW, screenH, bx + 20, by + 18, "=== AION SURREAL 3D BENCHMARK SCORECARD ===", 0xFF00FFCC, 1);
            DrawString(fb, screenW, screenH, bx + 20, by + 30, "--------------------------------------------", 0xFF336688, 1);

            std::snprintf(buf, sizeof(buf), "Frames Tested:      %d Frames (Swept Orbit)", benchResults->totalFrames);
            DrawString(fb, screenW, screenH, bx + 20, by + 46, buf, 0xFFE0E0E0, 1);

            std::snprintf(buf, sizeof(buf), "Average Framerate:  %.1f FPS  (Avg: %.2f ms)", benchResults->avgFps, benchResults->avgFrameTimeMs);
            DrawString(fb, screenW, screenH, bx + 20, by + 64, buf, 0xFF00FF88, 1);

            std::snprintf(buf, sizeof(buf), "Min / Max Framerate:%.1f FPS / %.1f FPS", benchResults->minFps, benchResults->maxFps);
            DrawString(fb, screenW, screenH, bx + 20, by + 82, buf, 0xFFFFCC00, 1);

            std::snprintf(buf, sizeof(buf), "Skeletal Skinning:  %.2f ms avg (61 Joints, 86k Verts)", benchResults->avgSkinningMs);
            DrawString(fb, screenW, screenH, bx + 20, by + 104, buf, 0xFF99BBEE, 1);

            std::snprintf(buf, sizeof(buf), "PBR Shading Pass:   %.2f ms avg (Cook-Torrance)", benchResults->avgRenderMs);
            DrawString(fb, screenW, screenH, bx + 20, by + 122, buf, 0xFF99BBEE, 1);

            std::snprintf(buf, sizeof(buf), "Meshlet Culling:    %.1f%% of Geometry Culled Off-Screen", benchResults->meshletCullingEfficiency);
            DrawString(fb, screenW, screenH, bx + 20, by + 144, buf, 0xFFFF007F, 1);

            std::snprintf(buf, sizeof(buf), "Engine Rating:      SOVEREIGN BARE-METAL GRADE (AAA HYBRID)");
            DrawString(fb, screenW, screenH, bx + 20, by + 172, buf, 0xFF00FFCC, 1);

            DrawString(fb, screenW, screenH, bx + 20, by + 204, "Press [B] to run again  |  Click anywhere to dismiss", 0xFF8899AA, 1);
        }
    }
};

} // namespace Surreal
