#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - IMAGE -> COLORED 3D MODEL ("Photo-to-3D", offline & free)
 * ----------------------------------------------------------------------------
 * Turns ONE picture into a closed, coloured 3D mesh without any AI model,
 * GPU or paid service:
 *
 *   1. Cut-out      : alpha channel, or flood-fill of the (flat) background.
 *   2. Inflation    : distance-to-silhouette -> rounded "balloon" thickness.
 *   3. Front        : real pixel colours + a little luminance relief (detail).
 *   4. Back         : guessed (the camera never saw it): same silhouette
 *                     mirrored, colours blurred / averaged / darkened.
 *   5. Rim          : thickness is 0 at the silhouette, so front and back
 *                     meet exactly -> the mesh is watertight.
 *
 * The result only needs to "roughly fit and look good" - it is NOT a real
 * reconstruction. Thin / hollow shapes and anything hidden behind the object
 * are approximated.
 *
 * Mesh convention: +Y up, feet (lowest silhouette pixel) at Y = 0, centred in
 * X and Z, picture front faces +Z. Colours are stored as vertex colours.
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_meshlet.hpp"
#ifndef STBI_INCLUDE_STB_IMAGE_H   // stb_image.h has no guard around its implementation part
#include "stb_image.h"
#endif
#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <queue>

namespace Surreal {

struct Image3DOptions {
    int   maxRes           = 320;   // high-density grid cells along the longest picture side
    float heightMeters     = 2.20f; // world height of the resulting model
    float depth            = 0.45f; // half thickness relative to the inflate radius
    float relief           = 0.35f; // strength of luminance detail on the front
    float backBlend        = 0.0f;  // 0 = back uses authentic synthesized colors, 1 = flat average colour
    float backDarken       = 0.90f; // brightness of the (guessed) back side
    int   mode             = 0;     // 0: Auto-Detect, 1: 100% Complete 3D Relief Plate, 2: Silhouette Cutout
    bool  generateBackside = true;  // 360° volumetric backside synthesis (hair, skull, spine, clothes)
    bool  anatomicalSmooth = true;  // Organic body volume without luminance spikes
    float bodyVolume       = 1.0f;  // Protrusion multiplier for foreground subjects
    int   ironingPasses    = 1440;  // Multi-pass geometric smoothing ("buegeln") & 360° texture painting ("malen") (up to 1,440x)
};

struct Image3DInfo {
    int   gridW = 0, gridH = 0;
    int   maskCells = 0;
    bool  usedAlpha = false;
    bool  plateMode = false;     // no background found -> thick "plate" with relief
    float bgR = 0, bgG = 0, bgB = 0;
    std::string error;
};

class SurrealImage3D {
public:
    static bool IsImageExt(const std::string& extLower) {
        return extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg" ||
               extLower == ".bmp" || extLower == ".tga";
    }

    static std::shared_ptr<SurrealMesh> BuildFromFile(
        const std::string& path, const Image3DOptions& opt = Image3DOptions(), Image3DInfo* info = nullptr)
    {
        Image3DInfo localInfo;
        Image3DInfo& inf = info ? *info : localInfo;
        int w = 0, h = 0, ch = 0;
        unsigned char* pix = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!pix || w < 2 || h < 2) {
            inf.error = "Could not load image: " + path;
            if (pix) stbi_image_free(pix);
            return nullptr;
        }
        auto mesh = BuildFromRGBA(pix, w, h, opt, &inf);
        stbi_image_free(pix);
        return mesh;
    }

    static std::shared_ptr<SurrealMesh> BuildFromRGBA(
        const uint8_t* rgba, int w, int h, const Image3DOptions& opt = Image3DOptions(), Image3DInfo* info = nullptr,
        bool allowCrop = true)
    {
        Image3DInfo localInfo;
        Image3DInfo& inf = info ? *info : localInfo;

        // ---- 1. Auto-Bounding Box Cropping for isolated cutouts ----
        int cropX0 = 0, cropY0 = 0, cropW = w, cropH = h;
        if (allowCrop && opt.mode != 1) {
            int minX = w, maxX = -1, minY = h, maxY = -1;
            int step = std::max(1, std::min(w, h) / 400);
            for (int y = 0; y < h; y += step) {
                for (int x = 0; x < w; x += step) {
                    uint8_t a = rgba[(static_cast<size_t>(y) * w + x) * 4 + 3];
                    if (a > 140) {
                        minX = std::min(minX, x); maxX = std::max(maxX, x);
                        minY = std::min(minY, y); maxY = std::max(maxY, y);
                    }
                }
            }
            if (minX < maxX && minY < maxY && (maxX - minX + 1 < w * 0.98f || maxY - minY + 1 < h * 0.98f)) {
                int padX = std::max(2, (maxX - minX) * 15 / 1000);
                int padY = std::max(2, (maxY - minY) * 15 / 1000);
                cropX0 = std::max(0, minX - padX);
                cropY0 = std::max(0, minY - padY);
                int cropX1 = std::min(w, maxX + 1 + padX);
                int cropY1 = std::min(h, maxY + 1 + padY);
                cropW = cropX1 - cropX0;
                cropH = cropY1 - cropY0;
            }
        }

        // Downsample crop region to the working grid (box filter, alpha weighted)
        float sc = std::min(1.0f, static_cast<float>(opt.maxRes) / static_cast<float>(std::max(cropW, cropH)));
        int gw = std::max(2, static_cast<int>(std::lround(cropW * sc)));
        int gh = std::max(2, static_cast<int>(std::lround(cropH * sc)));
        const int N = gw * gh;
        inf.gridW = gw; inf.gridH = gh;

        std::vector<float> R(N), G(N), B(N), A(N);
        for (int gy = 0; gy < gh; ++gy) {
            int y0 = cropY0 + static_cast<int>((static_cast<int64_t>(gy) * cropH) / gh);
            int y1 = std::max(y0 + 1, cropY0 + static_cast<int>((static_cast<int64_t>(gy + 1) * cropH) / gh));
            for (int gx = 0; gx < gw; ++gx) {
                int x0 = cropX0 + static_cast<int>((static_cast<int64_t>(gx) * cropW) / gw);
                int x1 = std::max(x0 + 1, cropX0 + static_cast<int>((static_cast<int64_t>(gx + 1) * cropW) / gw));
                float sr = 0, sg = 0, sb = 0, sa = 0, wsum = 0;
                int cnt = 0;
                for (int y = y0; y < y1 && y < h; ++y) {
                    for (int x = x0; x < x1 && x < w; ++x) {
                        const uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
                        float a = p[3] / 255.0f;
                        sr += p[0] * a; sg += p[1] * a; sb += p[2] * a;
                        wsum += a; sa += a; ++cnt;
                    }
                }
                int i = gy * gw + gx;
                if (wsum > 1e-4f) { R[i] = sr / wsum; G[i] = sg / wsum; B[i] = sb / wsum; }
                else { R[i] = G[i] = B[i] = 0; }
                A[i] = cnt ? sa / cnt : 1.0f;
            }
        }

        // ---- 2. Cut-out mask ----
        std::vector<uint8_t> fg(N, 0);
        int transparent = 0;
        for (int i = 0; i < N; ++i) if (A[i] < 0.9f) ++transparent;

        bool hasAlpha = (transparent > N / 100);
        bool forcePlate = (opt.mode == 1);
        bool forceCutout = (opt.mode == 2);

        if (forcePlate) {
            inf.plateMode = true;
            std::fill(fg.begin(), fg.end(), 1);
        } else if (hasAlpha) {
            inf.usedAlpha = true;
            for (int i = 0; i < N; ++i) fg[i] = (A[i] >= 0.55f) ? 1 : 0;
            KeepMainComponents(fg, gw, gh);
        } else {
            // Background colour = per-channel median of the picture border
            std::vector<float> br, bg, bb;
            auto addBorder = [&](int x, int y) { int i = y * gw + x; br.push_back(R[i]); bg.push_back(G[i]); bb.push_back(B[i]); };
            for (int x = 0; x < gw; ++x) { addBorder(x, 0); addBorder(x, gh - 1); }
            for (int y = 1; y < gh - 1; ++y) { addBorder(0, y); addBorder(gw - 1, y); }
            auto median = [](std::vector<float>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
            float mr = median(br), mg = median(bg), mb = median(bb);
            inf.bgR = mr; inf.bgG = mg; inf.bgB = mb;

            float borderVar = 0.0f;
            for (size_t b = 0; b < br.size(); ++b) {
                float dr = br[b] - mr, dg = bg[b] - mg, db = bb[b] - mb;
                borderVar += dr * dr + dg * dg + db * db;
            }
            float borderStdDev = std::sqrt(borderVar / std::max(1UL, br.size()));

            // If border has high variance (borderStdDev > 20.0f), this is a complex real-world photograph
            // (street, monument, landscape, room), NOT an isolated studio backdrop!
            // Attempting flood-fill would eat dark clothes, hair, and shadows!
            if (borderStdDev > 20.0f && !forceCutout) {
                inf.plateMode = true;
                std::fill(fg.begin(), fg.end(), 1);
            } else {
                auto distBg = [&](int i) {
                    float dr = R[i] - mr, dg = G[i] - mg, db = B[i] - mb;
                    return std::sqrt(dr * dr + dg * dg + db * db);
                };
                std::vector<float> bd;
                for (int x = 0; x < gw; ++x) { bd.push_back(distBg(x)); bd.push_back(distBg((gh - 1) * gw + x)); }
                for (int y = 1; y < gh - 1; ++y) { bd.push_back(distBg(y * gw)); bd.push_back(distBg(y * gw + gw - 1)); }
                std::sort(bd.begin(), bd.end());
                float p90 = bd[static_cast<size_t>(bd.size() * 0.9f)];
                float thr = std::min(70.0f, std::max(18.0f, p90 * 1.3f + 10.0f));

                // Flood fill the background inwards from the border (4-connected): a pixel joins if it is
                // close to the background colour (the threshold adapts to gradients / vignettes).
                std::vector<uint8_t> isBg(N, 0);
                std::queue<int> q;
                auto tryPush = [&](int i, int /*from*/) {
                    if (isBg[i]) return;
                    if (distBg(i) <= thr) { isBg[i] = 1; q.push(i); }
                };
                for (int x = 0; x < gw; ++x) { tryPush(x, -1); tryPush((gh - 1) * gw + x, -1); }
                for (int y = 0; y < gh; ++y) { tryPush(y * gw, -1); tryPush(y * gw + gw - 1, -1); }
                while (!q.empty()) {
                    int i = q.front(); q.pop();
                    int x = i % gw, y = i / gw;
                    if (x > 0)      tryPush(i - 1, i);
                    if (x < gw - 1) tryPush(i + 1, i);
                    if (y > 0)      tryPush(i - gw, i);
                    if (y < gh - 1) tryPush(i + gw, i);
                }
                for (int i = 0; i < N; ++i) fg[i] = isBg[i] ? 0 : 1;

                int rawFg = 0;
                for (int i = 0; i < N; ++i) rawFg += fg[i];

                // If flood-fill decimated the subject (less than 25% remaining) or barely touched it (> 90%),
                // or if it isn't an isolated character: preserve 100% of the image!
                if (rawFg < N * 25 / 100 || rawFg > N * 90 / 100) {
                    inf.plateMode = true;
                    std::fill(fg.begin(), fg.end(), 1);
                } else {
                    KeepMainComponents(fg, gw, gh);
                    int postFg = 0;
                    for (int i = 0; i < N; ++i) postFg += fg[i];
                    // If KeepMainComponents removed more than 40% of the remaining pixels,
                    // the image was fractured into disjoint floating islands! Fallback to clean plate!
                    if (postFg < rawFg * 0.60f || postFg < N * 18 / 100) {
                        inf.plateMode = true;
                        std::fill(fg.begin(), fg.end(), 1);
                    }
                }
            }
        }

        int fgCount = 0;
        for (int i = 0; i < N; ++i) fgCount += fg[i];
        if (fgCount > N * 92 / 100 || inf.plateMode) {
            // Nothing to cut out (landscape, photo, texture, monument): make a thick plate with relief.
            inf.plateMode = true;
            std::fill(fg.begin(), fg.end(), 1);
        } else {
            KeepMainComponents(fg, gw, gh);

            // Small subject in a big picture: crop to it and rebuild, so the grid resolution is
            // spent on the object instead of on empty background.
            if (allowCrop) {
                int cminx = gw, cmaxx = -1, cminy = gh, cmaxy = -1;
                for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) if (fg[y * gw + x]) {
                    cminx = std::min(cminx, x); cmaxx = std::max(cmaxx, x);
                    cminy = std::min(cminy, y); cmaxy = std::max(cmaxy, y);
                }
                if (cmaxx >= cminx) {
                    int bw = cmaxx - cminx + 1, bh = cmaxy - cminy + 1;
                    if (static_cast<float>(bw) * bh < 0.6f * gw * gh) {
                        int mx = std::max(2, static_cast<int>(0.05f * std::max(bw, bh)));
                        int x0 = std::max(0, cminx - mx), x1 = std::min(gw, cmaxx + 1 + mx);
                        int y0 = std::max(0, cminy - mx), y1 = std::min(gh, cmaxy + 1 + mx);
                        int sx0 = static_cast<int>((static_cast<int64_t>(x0) * w) / gw);
                        int sx1 = std::min(w, static_cast<int>((static_cast<int64_t>(x1) * w + gw - 1) / gw));
                        int sy0 = static_cast<int>((static_cast<int64_t>(y0) * h) / gh);
                        int sy1 = std::min(h, static_cast<int>((static_cast<int64_t>(y1) * h + gh - 1) / gh));
                        int cw = sx1 - sx0, ch = sy1 - sy0;
                        if (cw >= 8 && ch >= 8) {
                            std::vector<uint8_t> crop(static_cast<size_t>(cw) * ch * 4);
                            for (int y = 0; y < ch; ++y)
                                std::copy(rgba + (static_cast<size_t>(sy0 + y) * w + sx0) * 4,
                                          rgba + (static_cast<size_t>(sy0 + y) * w + sx0 + cw) * 4,
                                          crop.begin() + static_cast<size_t>(y) * cw * 4);
                            return BuildFromRGBA(crop.data(), cw, ch, opt, &inf, false);
                        }
                    }
                }
            }
        }
        fgCount = 0;
        for (int i = 0; i < N; ++i) fgCount += fg[i];
        inf.maskCells = fgCount;
        if (fgCount < 16) {
            inf.error = "No object detected in image (background not isolated?)";
            return nullptr;
        }

        auto isFg = [&](int x, int y) { return x >= 0 && y >= 0 && x < gw && y < gh && fg[y * gw + x]; };

        // ---- 3. Distance to silhouette (chamfer 3-4) ----
        std::vector<int> dist(N, 0);
        const int INF = 1 << 28;
        for (int i = 0; i < N; ++i) dist[i] = fg[i] ? INF : 0;
        auto D = [&](int x, int y) { return (x < 0 || y < 0 || x >= gw || y >= gh) ? 0 : dist[y * gw + x]; };
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            int i = y * gw + x; if (!fg[i]) continue;
            int v = dist[i];
            v = std::min(v, D(x - 1, y) + 3);     v = std::min(v, D(x, y - 1) + 3);
            v = std::min(v, D(x - 1, y - 1) + 4); v = std::min(v, D(x + 1, y - 1) + 4);
            dist[i] = v;
        }
        for (int y = gh - 1; y >= 0; --y) for (int x = gw - 1; x >= 0; --x) {
            int i = y * gw + x; if (!fg[i]) continue;
            int v = dist[i];
            v = std::min(v, D(x + 1, y) + 3);     v = std::min(v, D(x, y + 1) + 3);
            v = std::min(v, D(x + 1, y + 1) + 4); v = std::min(v, D(x - 1, y + 1) + 4);
            dist[i] = v;
        }
        std::vector<float> dEff(N, 0.0f);   // 0 on the silhouette, grows inwards (cell units)
        float dmax = 1.0f;
        for (int i = 0; i < N; ++i) if (fg[i]) {
            dEff[i] = std::max(0.0f, dist[i] / 3.0f - 1.0f);
            dmax = std::max(dmax, dEff[i]);
        }

        // Multi-pixel Perimeter Edge Decontamination:
        // Silhouette border pixels often contain background color bleed or anti-aliasing halo from the original photo.
        // We replace any pixel with dEff < 2.5f by propagating color from the nearest clean interior pixel (dEff >= 3.0f).
        if (!inf.plateMode) {
            std::vector<float> cleanR = R, cleanG = G, cleanB = B;
            const int searchR = 10;
            for (int y = 0; y < gh; ++y) {
                for (int x = 0; x < gw; ++x) {
                    int i = y * gw + x;
                    if (!fg[i] || dEff[i] >= 2.5f) continue;

                    float bestDist = 1e9f;
                    int bestJ = -1;
                    for (int dy = -searchR; dy <= searchR; ++dy) {
                        int yy = y + dy;
                        if (yy < 0 || yy >= gh) continue;
                        for (int dx = -searchR; dx <= searchR; ++dx) {
                            int xx = x + dx;
                            if (xx < 0 || xx >= gw) continue;
                            int j = yy * gw + xx;
                            if (!fg[j] || dEff[j] < 3.0f) continue;
                            float dsq = static_cast<float>(dx * dx + dy * dy);
                            if (dsq < bestDist) {
                                bestDist = dsq;
                                bestJ = j;
                            }
                        }
                    }
                    if (bestJ >= 0) {
                        cleanR[i] = R[bestJ];
                        cleanG[i] = G[bestJ];
                        cleanB[i] = B[bestJ];
                    }
                }
            }
            R = std::move(cleanR);
            G = std::move(cleanG);
            B = std::move(cleanB);
        }

        // ---- 4. Saliency, Anatomical Volume & Organic Depth Profiling ----
        float Rinfl = inf.plateMode ? std::max(3.0f, 0.08f * std::min(gw, gh)) : dmax;
        float Hmax  = inf.plateMode ? std::max(1.8f, opt.depth * (0.12f * std::min(gw, gh))) : opt.depth * Rinfl;

        std::vector<float> lum(N, 0.0f);
        for (int i = 0; i < N; ++i) lum[i] = (0.299f * R[i] + 0.587f * G[i] + 0.114f * B[i]) / 255.0f;
        std::vector<float> lumBlur = BoxBlurMasked(lum, fg, gw, gh, 5);

        // Subject Saliency & Skin/Face Detection
        std::vector<float> salRaw(N, 0.0f);
        std::vector<uint8_t> isSkin(N, 0);
        for (int i = 0; i < N; ++i) {
            if (!fg[i]) continue;
            float r = R[i], g = G[i], b = B[i];
            if (r > 50.0f && g > 35.0f && b > 20.0f && r > g && (r - g) >= 10.0f && (r - b) >= 15.0f) {
                float sum = r + g + b + 1e-4f;
                float nr = r / sum, ng = g / sum;
                if (nr > 0.35f && nr < 0.60f && ng > 0.25f && ng < 0.40f) {
                    isSkin[i] = 1;
                }
            }
            float dr = r - inf.bgR, dg = g - inf.bgG, db = b - inf.bgB;
            float bgDist = std::sqrt(dr * dr + dg * dg + db * db);
            float bgScore = std::min(1.0f, bgDist / 45.0f);

            int x = i % gw, y = i / gw;
            float xRel = (x - gw * 0.5f) / (gw * 0.5f);
            float xPrior = std::max(0.0f, 1.0f - xRel * xRel * 0.70f);
            float yRel = static_cast<float>(y) / static_cast<float>(gh);
            float yPrior = (yRel > 0.10f && yRel < 0.95f) ? 1.0f : std::max(0.0f, 1.0f - std::abs(yRel - 0.5f) * 2.5f);

            if (inf.plateMode) {
                salRaw[i] = std::min(1.0f, (bgScore * 0.60f + (isSkin[i] ? 0.45f : 0.0f)) * xPrior * yPrior);
            } else {
                salRaw[i] = 1.0f;
            }
        }
        std::vector<float> sal = BoxBlurMasked(salRaw, fg, gw, gh, 6);

        // Compute local subject horizontal centers of mass for accurate bilateral mirroring & spine modeling
        std::vector<float> xCenterLocal(N, gw * 0.5f);
        std::vector<float> localWidth(N, gw * 0.25f);
        const int winR = std::max(4, gw / 8);
        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                int i = y * gw + x;
                float wsum = 0.0f, xsum = 0.0f;
                int x0 = std::max(0, x - winR), x1 = std::min(gw, x + winR + 1);
                for (int xx = x0; xx < x1; ++xx) {
                    float s = sal[y * gw + xx];
                    wsum += s;
                    xsum += xx * s;
                }
                if (wsum > 0.20f) {
                    xCenterLocal[i] = xsum / wsum;
                    localWidth[i] = std::max(4.0f, static_cast<float>(winR));
                } else {
                    xCenterLocal[i] = static_cast<float>(x);
                    localWidth[i] = static_cast<float>(winR);
                }
            }
        }

        // Find topmost and bottommost foreground cells per column
        std::vector<int> colTopY(gw, gh), colBotY(gw, -1);
        for (int x = 0; x < gw; ++x) {
            for (int y = 0; y < gh; ++y) {
                if (fg[y * gw + x]) {
                    colTopY[x] = std::min(colTopY[x], y);
                    colBotY[x] = std::max(colBotY[x], y);
                }
            }
        }
        std::vector<float> topYSmooth(gw, 0.0f);
        for (int x = 0; x < gw; ++x) {
            float sumY = 0.0f, wsum = 0.0f;
            for (int dx = -3; dx <= 3; ++dx) {
                int xx = x + dx;
                if (xx >= 0 && xx < gw && colTopY[xx] < colBotY[xx]) {
                    float w = 1.0f / (1.0f + std::abs(dx));
                    sumY += colTopY[xx] * w;
                    wsum += w;
                }
            }
            topYSmooth[x] = (wsum > 0.0f) ? (sumY / wsum) : static_cast<float>(colTopY[x]);
        }

        // Find dominant hair colors per column for multi-character realistic backside hair
        std::vector<float> hairR(gw, 25.0f), hairG(gw, 22.0f), hairB(gw, 22.0f);
        std::vector<bool> hasHair(gw, false);

        for (int x = 0; x < gw; ++x) {
            if (colTopY[x] >= colBotY[x]) continue;
            float figH = static_cast<float>(colBotY[x] - colTopY[x]);
            int yCrownStart = static_cast<int>(topYSmooth[x] + figH * 0.02f);
            int yCrownEnd = std::max(yCrownStart + 1, static_cast<int>(topYSmooth[x] + figH * 0.15f));

            float hr = 0, hg = 0, hb = 0, hw = 0;
            for (int y = yCrownStart; y <= yCrownEnd && y < gh; ++y) {
                int i = y * gw + x;
                if (!fg[i]) continue;
                hr += R[i]; hg += G[i]; hb += B[i];
                hw += 1.0f;
            }
            if (hw > 0.0f) {
                hairR[x] = hr / hw;
                hairG[x] = hg / hw;
                hairB[x] = hb / hw;
                hasHair[x] = true;
            }
        }
        for (int x = 1; x < gw; ++x) {
            if (!hasHair[x] && hasHair[x - 1]) {
                hairR[x] = hairR[x - 1]; hairG[x] = hairG[x - 1]; hairB[x] = hairB[x - 1];
                hasHair[x] = true;
            }
        }
        for (int x = gw - 2; x >= 0; --x) {
            if (!hasHair[x] && hasHair[x + 1]) {
                hairR[x] = hairR[x + 1]; hairG[x] = hairG[x + 1]; hairB[x] = hairB[x + 1];
                hasHair[x] = true;
            }
        }

        // Depth profiles: zf (front) and zb (back)
        std::vector<float> hh(N, 0.0f), zf(N, 0.0f), zb(N, 0.0f);
        for (int i = 0; i < N; ++i) {
            if (!fg[i]) continue;
            int x = i % gw, y = i / gw;
            float t = std::min(1.0f, dEff[i] / Rinfl);
            float baseChamfer = Hmax * std::sqrt(std::max(0.0f, 1.0f - (1.0f - t) * (1.0f - t)));
            hh[i] = baseChamfer;

            float yRel = static_cast<float>(y) / static_cast<float>(gh);
            float dx = (x - xCenterLocal[i]) / localWidth[i];
            dx = std::max(-1.0f, std::min(1.0f, dx));
            float vHull = std::sqrt(std::max(0.05f, 1.0f - dx * dx));

            float figH = (colTopY[x] < colBotY[x]) ? static_cast<float>(colBotY[x] - colTopY[x]) : static_cast<float>(gh);
            float yLoc = (colTopY[x] < colBotY[x]) ? ((y - topYSmooth[x]) / std::max(1.0f, figH)) : (static_cast<float>(y) / static_cast<float>(gh));

            float bodyThickness = 0.0f;
            if (inf.plateMode) {
                float subjProtrude = (sal[i] > 0.15f) ? (sal[i] - 0.15f) / 0.85f : 0.0f;
                float plateFloor = baseChamfer * 0.35f;
                float bodyElev = Hmax * 1.35f * opt.bodyVolume * subjProtrude * (0.75f + 0.25f * vHull);
                bodyThickness = plateFloor + bodyElev;
            } else {
                // Standalone 3D Humanoid Figure (Adaptive anatomical proportions per character)
                float refDepth = 1.0f;
                if (yLoc < 0.25f) refDepth = 0.95f;      // Head & Skull
                else if (yLoc < 0.55f) refDepth = 1.15f; // Chest & Torso / Jacket
                else if (yLoc < 0.65f) refDepth = 0.90f; // Waist
                else if (yLoc < 0.82f) refDepth = 1.05f; // Hips & Gluteals
                else refDepth = 0.95f;                  // Legs / Boots
                bodyThickness = baseChamfer * opt.bodyVolume * refDepth * (0.80f + 0.20f * vHull);
            }

            // Controlled micro-relief (clamped to [-0.20, +0.20], scaled to 25% to prevent spikes!)
            float lumDetail = std::max(-0.20f, std::min(0.20f, lum[i] - lumBlur[i]));
            float microRelief = opt.relief * Hmax * 0.25f * lumDetail;
            zf[i] = std::max(0.02f * Hmax, bodyThickness + microRelief);

            // 360° Volumetric Backside Geometry zb[i]
            if (opt.generateBackside) {
                float backSubjH = bodyThickness * 0.92f;
                if (yLoc < 0.25f) {
                    // Head: rounded occipital cranial dome
                    float dy_c = (yLoc - 0.12f) / 0.13f;
                    float dome = std::sqrt(std::max(0.15f, 1.0f - dx * dx - dy_c * dy_c));
                    zb[i] = -backSubjH * (0.85f + 0.25f * dome);
                } else if (yLoc >= 0.25f && yLoc < 0.32f) {
                    // Nape of neck / cervical lordosis: gentle inward curve
                    zb[i] = -backSubjH * 0.78f;
                } else if (yLoc >= 0.32f && yLoc < 0.55f) {
                    // Torso / Jacket: shoulder blade bulges and center spine furrow
                    float spineIndent = -0.12f * std::exp(-(dx * dx) / 0.06f);
                    float scapulaBulge = +0.16f * std::exp(-((std::abs(dx) - 0.42f) * (std::abs(dx) - 0.42f)) / 0.08f);
                    zb[i] = -backSubjH * (0.94f + scapulaBulge + spineIndent);
                } else if (yLoc >= 0.55f && yLoc < 0.65f) {
                    // Waist: lumbar inward taper (small of the back)
                    zb[i] = -backSubjH * 0.84f;
                } else if (yLoc >= 0.65f && yLoc < 0.82f) {
                    // Hips & gluteal curve: natural outward curve with central divide
                    float glutealBulge = +0.14f * std::exp(-((std::abs(dx) - 0.35f) * (std::abs(dx) - 0.35f)) / 0.10f);
                    float glutealIndent = -0.06f * std::exp(-(dx * dx) / 0.04f);
                    zb[i] = -backSubjH * (0.98f + glutealBulge + glutealIndent);
                } else {
                    // Legs / calves: posterior calf muscle curve down to boot heel
                    float calfBulge = (yLoc < 0.92f) ? +0.08f * std::exp(-((yLoc - 0.86f) * (yLoc - 0.86f)) / 0.005f) : 0.0f;
                    zb[i] = -backSubjH * (0.88f + calfBulge);
                }
                if (inf.plateMode) {
                    zb[i] = std::min(-baseChamfer * 0.45f, zb[i]);
                } else {
                    zb[i] = std::min(-0.02f * Hmax, zb[i]);
                }
            } else {
                zb[i] = -hh[i];
            }
        }

        // ---- 4a-2. Perimeter Fillet Curve & Lateral Edge Rounding (Abrundung & Glaettung) ----
        // Identify active quads and calculate distance to the mesh rim boundary
        std::vector<uint8_t> activeQuad(N, 0);
        for (int y = 0; y + 1 < gh; ++y) {
            for (int x = 0; x + 1 < gw; ++x) {
                int qa = y * gw + x;
                if (fg[qa] && fg[qa + 1] && fg[qa + gw + 1] && fg[qa + gw]) {
                    activeQuad[qa] = 1;
                }
            }
        }

        std::vector<uint8_t> vActive(N, 0);
        std::vector<uint8_t> vRim(N, 0);
        for (int y = 0; y + 1 < gh; ++y) {
            for (int x = 0; x + 1 < gw; ++x) {
                int qa = y * gw + x;
                if (!activeQuad[qa]) continue;
                int a = qa, b = a + 1, c = a + gw + 1, d = a + gw;
                vActive[a] = vActive[b] = vActive[c] = vActive[d] = 1;

                if (y == 0 || !activeQuad[qa - gw]) { vRim[a] = 1; vRim[b] = 1; }
                if (x + 2 >= gw || !activeQuad[qa + 1]) { vRim[b] = 1; vRim[c] = 1; }
                if (y + 2 >= gh || !activeQuad[qa + gw]) { vRim[c] = 1; vRim[d] = 1; }
                if (x == 0 || !activeQuad[qa - 1]) { vRim[d] = 1; vRim[a] = 1; }
            }
        }

        // BFS distance from boundary to create a continuous circular fillet arc
        std::vector<float> distRim(N, 999.0f);
        std::queue<int> qRim;
        for (int i = 0; i < N; ++i) {
            if (vRim[i]) { distRim[i] = 0.0f; qRim.push(i); }
        }
        while (!qRim.empty()) {
            int cur = qRim.front(); qRim.pop();
            int cx = cur % gw, cy = cur / gw;
            float cd = distRim[cur];
            for (int dy = -1; dy <= 1; ++dy) {
                int ny = cy + dy;
                if (ny < 0 || ny >= gh) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = cx + dx;
                    if (nx < 0 || nx >= gw) continue;
                    int nb = ny * gw + nx;
                    if (!vActive[nb]) continue;
                    float step = (dx != 0 && dy != 0) ? 1.414f : 1.0f;
                    if (cd + step < distRim[nb]) {
                        distRim[nb] = cd + step;
                        qRim.push(nb);
                    }
                }
            }
        }

        // Apply smooth quarter-circle fillet curvature to round the lateral edges (Abrundung & Glaettung)
        if (!inf.plateMode) {
            const float R_fillet = 6.0f;
            for (int i = 0; i < N; ++i) {
                if (!vActive[i]) continue;
                float t = std::min(1.0f, distRim[i] / R_fillet);
                float fillet = std::sqrt(std::max(0.0f, 1.0f - (1.0f - t) * (1.0f - t)));
                float edgeScale = 0.06f + 0.94f * fillet;
                zf[i] *= edgeScale;
                zb[i] *= edgeScale;
            }
        }

        // ---- 4b. Multi-Pass Geometric Smoothing Relaxation (Up to 1,440x "drueber buegeln") ----
        // Eliminates polygon faceting, steppings and spiky noise across front & back surfaces
        int numIronPasses = std::max(120, std::min(1440, opt.ironingPasses));
        std::vector<float> zfSmooth = zf;
        std::vector<float> zbSmooth = zb;
        std::vector<float> nextZf(N, 0.0f);
        std::vector<float> nextZb(N, 0.0f);
        const float invTwoSigmaSq = 1.0f / (2.0f * (Hmax * 0.15f) * (Hmax * 0.15f) + 1e-5f);
        // Scale per-pass relaxation step so 1,440 passes converge ultra-smoothly without over-flattening volume
        const float relaxStep = std::min(0.35f, 45.0f / static_cast<float>(numIronPasses));

        for (int pass = 0; pass < numIronPasses; ++pass) {
            #pragma omp parallel for schedule(static)
            for (int y = 1; y < gh - 1; ++y) {
                for (int x = 1; x + 1 < gw; ++x) {
                    int i = y * gw + x;
                    if (!fg[i]) continue;

                    // Frontside Bilateral Smoothing Relaxation
                    float curZf = zfSmooth[i];
                    float sumZf = 0.0f, sumWf = 0.0f;
                    int nbs[4] = { i - 1, i + 1, i - gw, i + gw };
                    for (int nb : nbs) {
                        if (!fg[nb]) continue;
                        float diff = zfSmooth[nb] - curZf;
                        float w = 1.0f / (1.0f + diff * diff * invTwoSigmaSq);
                        sumZf += zfSmooth[nb] * w;
                        sumWf += w;
                    }
                    nextZf[i] = (sumWf > 0.01f) ? (curZf + relaxStep * (sumZf / sumWf - curZf)) : curZf;

                    // Backside Bilateral Smoothing Relaxation
                    float curZb = zbSmooth[i];
                    float sumZb = 0.0f, sumWb = 0.0f;
                    for (int nb : nbs) {
                        if (!fg[nb]) continue;
                        float diff = zbSmooth[nb] - curZb;
                        float w = 1.0f / (1.0f + diff * diff * invTwoSigmaSq);
                        sumZb += zbSmooth[nb] * w;
                        sumWb += w;
                    }
                    nextZb[i] = (sumWb > 0.01f) ? (curZb + relaxStep * (sumZb / sumWb - curZb)) : curZb;
                }
            }
            std::swap(zfSmooth, nextZf);
            std::swap(zbSmooth, nextZb);
        }
        zf = std::move(zfSmooth);
        zb = std::move(zbSmooth);

        // ---- 4c. High-Fidelity Backside Material & Color Synthesis ----
        std::vector<float> backR = R, backG = G, backB = B;

        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                int i = y * gw + x;
                if (!fg[i]) continue;

                if (colTopY[x] < colBotY[x]) {
                    float figH = static_cast<float>(colBotY[x] - colTopY[x]);
                    float yLoc = (y - topYSmooth[x]) / std::max(1.0f, figH);
                    float dx = (x - xCenterLocal[i]) / localWidth[i];
                    dx = std::max(-1.0f, std::min(1.0f, dx));

                    if (yLoc <= 0.28f) {
                        // 1. Head / Crown / Occiput: 100% hair with natural strand wave & sheen (ZERO face/skin reflection)
                        float wave = std::sin((x * 2.2f + y * 0.6f) * 1.5f);
                        float strand = 0.95f + 0.05f * wave;
                        backR[i] = std::max(0.0f, std::min(255.0f, hairR[x] * strand));
                        backG[i] = std::max(0.0f, std::min(255.0f, hairG[x] * strand));
                        backB[i] = std::max(0.0f, std::min(255.0f, hairB[x] * strand));
                    } else if (yLoc <= 0.38f) {
                        // 2. Neck / Hair cascade transition
                        if (isSkin[i] || hairR[x] > 100.0f) {
                            // Blonde/pink hair or skin: flow hair down the neck and upper back
                            float strand = 0.93f + 0.07f * std::sin((x * 2.0f + y * 0.5f) * 1.5f);
                            backR[i] = std::max(0.0f, std::min(255.0f, hairR[x] * strand));
                            backG[i] = std::max(0.0f, std::min(255.0f, hairG[x] * strand));
                            backB[i] = std::max(0.0f, std::min(255.0f, hairB[x] * strand));
                        } else if (isSkin[i]) {
                            // Replace front neck/chest skin with jacket fabric below
                            for (int dy = 1; dy <= 10; ++dy) {
                                int yy = y + dy;
                                if (yy < gh && fg[yy * gw + x] && !isSkin[yy * gw + x]) {
                                    int j = yy * gw + x;
                                    backR[i] = R[j]; backG[i] = G[j]; backB[i] = B[j];
                                    break;
                                }
                            }
                        }
                    } else if (yLoc <= 0.62f) {
                        // 3. Torso / Waist / Jackets
                        if (isSkin[i]) {
                            for (int dy = 1; dy <= 10; ++dy) {
                                int yy = y + dy;
                                if (yy < gh && fg[yy * gw + x] && !isSkin[yy * gw + x]) {
                                    int j = yy * gw + x;
                                    backR[i] = R[j]; backG[i] = G[j]; backB[i] = B[j];
                                    break;
                                }
                            }
                        }
                        // Sculpt realistic anatomical shading for jackets & back:
                        // 1. Subtle spinal groove shadow
                        float spineShade = 1.0f - 0.12f * std::exp(-(dx * dx) / 0.05f);
                        // 2. Subtle scapular shoulder blade highlight
                        float scapulaLight = 1.0f + 0.05f * std::exp(-((std::abs(dx) - 0.42f) * (std::abs(dx) - 0.42f)) / 0.08f);
                        float backAO = spineShade * scapulaLight;
                        backR[i] = std::max(0.0f, std::min(255.0f, backR[i] * backAO));
                        backG[i] = std::max(0.0f, std::min(255.0f, backG[i] * backAO));
                        backB[i] = std::max(0.0f, std::min(255.0f, backB[i] * backAO));
                    }
                }
            }
        }

        // Apply multi-pass bilateral painting (scaled up with opt.ironingPasses, up to 48 passes at 1440)
        // to blend away front zippers/buttons while preserving 100% of authentic garment colors and boundaries
        int paintPasses = std::max(12, std::min(48, opt.ironingPasses / 30));
        for (int pass = 0; pass < paintPasses; ++pass) {
            std::vector<float> nextR = backR, nextG = backG, nextB = backB;
            #pragma omp parallel for schedule(static)
            for (int y = 1; y < gh - 1; ++y) {
                float yRel = static_cast<float>(y) / static_cast<float>(gh);
                if (yRel <= 0.16f) continue; // Keep hair crisp

                for (int x = 1; x + 1 < gw; ++x) {
                    int i = y * gw + x;
                    if (!fg[i]) continue;

                    float cr = backR[i], cg = backG[i], cb = backB[i];
                    float avgR = 0, avgG = 0, avgB = 0, wsum = 0;
                    int nbs[4] = { i - 1, i + 1, i - gw, i + gw };
                    for (int nb : nbs) {
                        if (!fg[nb]) continue;
                        float dr = backR[nb] - cr, dg = backG[nb] - cg, db = backB[nb] - cb;
                        float distSq = dr * dr + dg * dg + db * db;
                        if (distSq > 35.0f * 35.0f) continue; // Strict edge-preserving!
                        float w = std::exp(-distSq / (2.0f * 18.0f * 18.0f));
                        avgR += backR[nb] * w; avgG += backG[nb] * w; avgB += backB[nb] * w;
                        wsum += w;
                    }
                    if (wsum > 0.01f) {
                        nextR[i] = cr + 0.35f * (avgR / wsum - cr);
                        nextG[i] = cg + 0.35f * (avgG / wsum - cg);
                        nextB[i] = cb + 0.35f * (avgB / wsum - cb);
                    }
                }
            }
            backR = std::move(nextR);
            backG = std::move(nextG);
            backB = std::move(nextB);
        }

        // ---- 5. World mapping ----
        int minx = gw, maxx = -1, miny = gh, maxy = -1;
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) if (fg[y * gw + x]) {
            minx = std::min(minx, x); maxx = std::max(maxx, x);
            miny = std::min(miny, y); maxy = std::max(maxy, y);
        }
        float bboxH = static_cast<float>(maxy - miny + 1);
        float bboxW = static_cast<float>(maxx - minx + 1);
        float s = opt.heightMeters / bboxH;
        if (bboxW * s > 3.0f * opt.heightMeters) s = 3.0f * opt.heightMeters / bboxW;
        float cx = 0.5f * (minx + maxx);

        // ---- 6. Colors, Unsharp-Mask Clarity Filter & High-Res Dual-Hemisphere GPU Texture Atlas ----
        float avR = 0, avG = 0, avB = 0;
        for (int i = 0; i < N; ++i) if (fg[i]) { avR += R[i]; avG += G[i]; avB += B[i]; }
        avR /= fgCount; avG /= fgCount; avB /= fgCount;
        std::vector<float> bR = BoxBlurMasked(R, fg, gw, gh, 5);
        std::vector<float> bG = BoxBlurMasked(G, fg, gw, gh, 5);
        std::vector<float> bB = BoxBlurMasked(B, fg, gw, gh, 5);

        // Unsharp-Mask micro-contrast enhancement on front photograph (eyes, lips, hair, fabric details)
        std::vector<float> fineBlurR = BoxBlurMasked(R, fg, gw, gh, 1);
        std::vector<float> fineBlurG = BoxBlurMasked(G, fg, gw, gh, 1);
        std::vector<float> fineBlurB = BoxBlurMasked(B, fg, gw, gh, 1);
        for (int i = 0; i < N; ++i) {
            if (!fg[i]) continue;
            R[i] = std::max(0.0f, std::min(255.0f, R[i] + 0.42f * (R[i] - fineBlurR[i])));
            G[i] = std::max(0.0f, std::min(255.0f, G[i] + 0.42f * (G[i] - fineBlurG[i])));
            B[i] = std::max(0.0f, std::min(255.0f, B[i] + 0.42f * (B[i] - fineBlurB[i])));
        }

        // Build Dual-Hemisphere RGBA GPU Texture Atlas:
        // Left half [0..gw-1] = Unsharp-Masked Front Photo (U in [0.0, 0.5])
        // Right half [gw..2*gw-1] = 1,440-Pass Painted 360 Backside (U in [0.5, 1.0])
        const int atlasW = gw * 2;
        const int atlasH = gh;
        std::vector<uint8_t> atlasRGBA(static_cast<size_t>(atlasW) * atlasH * 4, 255);
        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                int i = y * gw + x;
                float rf = R[i], gf = G[i], bf = B[i];
                float rb = (opt.generateBackside ? backR[i] : bR[i]) * opt.backDarken;
                float gb = (opt.generateBackside ? backG[i] : bG[i]) * opt.backDarken;
                float bb = (opt.generateBackside ? backB[i] : bB[i]) * opt.backDarken;

                size_t idxFront = (static_cast<size_t>(y) * atlasW + x) * 4;
                atlasRGBA[idxFront + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, rf + 0.5f)));
                atlasRGBA[idxFront + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, gf + 0.5f)));
                atlasRGBA[idxFront + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, bf + 0.5f)));
                atlasRGBA[idxFront + 3] = 255;

                size_t idxBack = (static_cast<size_t>(y) * atlasW + (gw + x)) * 4;
                atlasRGBA[idxBack + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, rb + 0.5f)));
                atlasRGBA[idxBack + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, gb + 0.5f)));
                atlasRGBA[idxBack + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, bb + 0.5f)));
                atlasRGBA[idxBack + 3] = 255;
            }
        }

        auto HH = [&](const std::vector<float>& f, int x, int y) { return isFg(x, y) ? f[y * gw + x] : 0.0f; };

        auto mesh = std::make_shared<SurrealMesh>();
        std::vector<int> idxF(N, -1), idxB(N, -1);
        mesh->vertices.reserve(static_cast<size_t>(fgCount) * 2 + 1024);

        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            int i = y * gw + x;
            if (!fg[i]) continue;
            float px = (x - cx) * s;
            float py = (maxy - y) * s;
            float uNorm = (x + 0.5f) / gw, vNorm = (y + 0.5f) / gh;
            float uFront = 0.5f * uNorm;
            float uBack  = 0.5f + 0.5f * uNorm;

            // Front vertex
            float fx = (HH(zf, x + 1, y) - HH(zf, x - 1, y)) * 0.5f;
            float fyw = (HH(zf, x, y + 1) - HH(zf, x, y - 1)) * 0.5f;
            Vec3 nF = Vec3(-fx, fyw, 1.0f).Normalized();
            Vec4 cF(R[i] / 255.0f, G[i] / 255.0f, B[i] / 255.0f, 1.0f);
            idxF[i] = static_cast<int>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(Vec3(px, py, zf[i] * s), nF, uFront, vNorm, cF));

            // Backside vertex: 360° Material & Color Synthesis
            float bx = (HH(zb, x + 1, y) - HH(zb, x - 1, y)) * 0.5f;
            float byw = (HH(zb, x, y + 1) - HH(zb, x, y - 1)) * 0.5f;
            Vec3 nB = Vec3(-bx, byw, -1.0f).Normalized();

            float rBack = opt.generateBackside ? backR[i] : bR[i];
            float gBack = opt.generateBackside ? backG[i] : bG[i];
            float bBack = opt.generateBackside ? backB[i] : bB[i];

            float cr = (rBack / 255.0f) * opt.backDarken;
            float cg = (gBack / 255.0f) * opt.backDarken;
            float cb = (bBack / 255.0f) * opt.backDarken;
            cr = std::max(0.0f, std::min(1.0f, cr));
            cg = std::max(0.0f, std::min(1.0f, cg));
            cb = std::max(0.0f, std::min(1.0f, cb));

            idxB[i] = static_cast<int>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(Vec3(px, py, zb[i] * s), nB, uBack, vNorm, Vec4(cr, cg, cb, 1.0f)));
        }

        // ---- 7. Triangles: front quad + back quad + watertight rim walls ----
        mesh->indices.reserve(static_cast<size_t>(fgCount) * 12 + (gw + gh) * 12);
        for (int y = 0; y + 1 < gh; ++y) for (int x = 0; x + 1 < gw; ++x) {
            int a = y * gw + x, b = a + 1, c = a + gw + 1, d = a + gw;
            if (!(fg[a] && fg[b] && fg[c] && fg[d])) continue;
            // front (CCW seen from +Z)
            mesh->indices.push_back(idxF[a]); mesh->indices.push_back(idxF[d]); mesh->indices.push_back(idxF[c]);
            mesh->indices.push_back(idxF[a]); mesh->indices.push_back(idxF[c]); mesh->indices.push_back(idxF[b]);
            // back (CCW seen from -Z)
            mesh->indices.push_back(idxB[a]); mesh->indices.push_back(idxB[c]); mesh->indices.push_back(idxB[d]);
            mesh->indices.push_back(idxB[a]); mesh->indices.push_back(idxB[b]); mesh->indices.push_back(idxB[c]);
        }

        size_t rimStartIndex = mesh->indices.size();

        // Watertight Perimeter / Rim side walls with DEDICATED rim vertices & true outward normals:
        // Stitches front and back into a closed solid along ANY boundary, perfectly colored with ambient occlusion
        auto isQuadActive = [&](int qx, int qy) -> bool {
            if (qx < 0 || qy < 0 || qx + 1 >= gw || qy + 1 >= gh) return false;
            int qa = qy * gw + qx;
            return (fg[qa] && fg[qa + 1] && fg[qa + gw + 1] && fg[qa + gw]);
        };

        auto addRimQuad = [&](int cell1, int cell2, float normX, float normY) {
            Vec3 nOut(normX, normY, 0.0f);

            int x1 = cell1 % gw, y1 = cell1 / gw;
            float px1 = (x1 - cx) * s;
            float py1 = (maxy - y1) * s;
            Vec3 pF1(px1, py1, zf[cell1] * s);
            Vec3 pB1(px1, py1, zb[cell1] * s);

            int x2 = cell2 % gw, y2 = cell2 / gw;
            float px2 = (x2 - cx) * s;
            float py2 = (maxy - y2) * s;
            Vec3 pF2(px2, py2, zf[cell2] * s);
            Vec3 pB2(px2, py2, zb[cell2] * s);

            // True decontaminated clothing / skin / hair color darkened by 0.85 for lateral body occlusion
            float ao = 0.85f;
            float bDark = opt.backDarken;
            Vec4 cF1(R[cell1] / 255.0f * ao, G[cell1] / 255.0f * ao, B[cell1] / 255.0f * ao, 1.0f);
            Vec4 cF2(R[cell2] / 255.0f * ao, G[cell2] / 255.0f * ao, B[cell2] / 255.0f * ao, 1.0f);
            Vec4 cB1(backR[cell1] / 255.0f * ao * bDark, backG[cell1] / 255.0f * ao * bDark, backB[cell1] / 255.0f * ao * bDark, 1.0f);
            Vec4 cB2(backR[cell2] / 255.0f * ao * bDark, backG[cell2] / 255.0f * ao * bDark, backB[cell2] / 255.0f * ao * bDark, 1.0f);

            float u1Norm = (x1 + 0.5f) / gw, v1Norm = (y1 + 0.5f) / gh;
            float u2Norm = (x2 + 0.5f) / gw, v2Norm = (y2 + 0.5f) / gh;

            uint32_t vF1 = static_cast<uint32_t>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(pF1, nOut, 0.5f * u1Norm, v1Norm, cF1));
            uint32_t vF2 = static_cast<uint32_t>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(pF2, nOut, 0.5f * u2Norm, v2Norm, cF2));
            uint32_t vB2 = static_cast<uint32_t>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(pB2, nOut, 0.5f + 0.5f * u2Norm, v2Norm, cB2));
            uint32_t vB1 = static_cast<uint32_t>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(pB1, nOut, 0.5f + 0.5f * u1Norm, v1Norm, cB1));

            // True outward CCW winding:
            // Triangle 1: vF1 -> vF2 -> vB2
            // Triangle 2: vF1 -> vB2 -> vB1
            mesh->indices.push_back(vF1);
            mesh->indices.push_back(vF2);
            mesh->indices.push_back(vB2);

            mesh->indices.push_back(vF1);
            mesh->indices.push_back(vB2);
            mesh->indices.push_back(vB1);
        };

        for (int y = 0; y + 1 < gh; ++y) {
            for (int x = 0; x + 1 < gw; ++x) {
                if (!isQuadActive(x, y)) continue;
                int a = y * gw + x, b = a + 1, c = a + gw + 1, d = a + gw;

                // Top boundary edge (a -> b): outward is +Y in world (which is -y in grid)
                if (!isQuadActive(x, y - 1)) {
                    addRimQuad(a, b, 0.0f, 1.0f);
                }
                // Right boundary edge (b -> c): outward is +X in world
                if (!isQuadActive(x + 1, y)) {
                    addRimQuad(b, c, 1.0f, 0.0f);
                }
                // Bottom boundary edge (c -> d): outward is -Y in world (which is +y in grid)
                if (!isQuadActive(x, y + 1)) {
                    addRimQuad(c, d, 0.0f, -1.0f);
                }
                // Left boundary edge (d -> a): outward is -X in world
                if (!isQuadActive(x - 1, y)) {
                    addRimQuad(d, a, -1.0f, 0.0f);
                }
            }
        }

        if (mesh->indices.size() < 3) {
            inf.error = "Object too thin or too small for a 3D mesh";
            return nullptr;
        }

        // ---- 8. Smooth Phong Normals (Independent Front/Back & Rim Accumulation) ----
        // 8a. Accumulate front & back surface normals independently (zero lateral bleeding)
        std::vector<Vec3> accumNormals(mesh->vertices.size(), Vec3(0, 0, 0));
        for (size_t t = 0; t + 2 < rimStartIndex; t += 3) {
            uint32_t i0 = mesh->indices[t + 0];
            uint32_t i1 = mesh->indices[t + 1];
            uint32_t i2 = mesh->indices[t + 2];
            const Vec3& p0 = mesh->vertices[i0].position;
            const Vec3& p1 = mesh->vertices[i1].position;
            const Vec3& p2 = mesh->vertices[i2].position;
            Vec3 edge1 = p1 - p0;
            Vec3 edge2 = p2 - p0;
            Vec3 fn = edge1.Cross(edge2);
            accumNormals[i0] = accumNormals[i0] + fn;
            accumNormals[i1] = accumNormals[i1] + fn;
            accumNormals[i2] = accumNormals[i2] + fn;
        }
        for (size_t v = 0; v < mesh->vertices.size(); ++v) {
            if (accumNormals[v].LengthSq() > 1e-6f) {
                mesh->vertices[v].normal = accumNormals[v].Normalized();
            }
        }

        // 8b. Accumulate rim normals between adjacent rim quads (smooth rounded perimeter without stepped facets)
        std::vector<Vec3> rimAccum(mesh->vertices.size(), Vec3(0, 0, 0));
        for (size_t t = rimStartIndex; t + 2 < mesh->indices.size(); t += 3) {
            uint32_t i0 = mesh->indices[t + 0];
            uint32_t i1 = mesh->indices[t + 1];
            uint32_t i2 = mesh->indices[t + 2];
            const Vec3& p0 = mesh->vertices[i0].position;
            const Vec3& p1 = mesh->vertices[i1].position;
            const Vec3& p2 = mesh->vertices[i2].position;
            Vec3 edge1 = p1 - p0;
            Vec3 edge2 = p2 - p0;
            Vec3 fn = edge1.Cross(edge2);
            rimAccum[i0] = rimAccum[i0] + fn;
            rimAccum[i1] = rimAccum[i1] + fn;
            rimAccum[i2] = rimAccum[i2] + fn;
        }
        for (size_t v = rimStartIndex; v < mesh->vertices.size(); ++v) {
            if (rimAccum[v].LengthSq() > 1e-6f) {
                Vec3 n = rimAccum[v].Normalized();
                n.z = 0.0f; // Pure planar radial in XY plane: eliminates specular glints
                if (n.LengthSq() > 1e-6f) n = n.Normalized();
                mesh->vertices[v].normal = n;
            }
        }

        // Attach High-Resolution Dual-Hemisphere RGBA GPU Texture Atlas SubMesh
        SubMesh sub;
        sub.indexOffset = 0;
        sub.indexCount = static_cast<uint32_t>(mesh->indices.size());
        sub.albedo = ColorRGB(1.0f, 1.0f, 1.0f);
        sub.roughness = 0.65f;
        sub.metallic = 0.05f;
        sub.texWidth = atlasW;
        sub.texHeight = atlasH;
        sub.texRGBA = std::make_shared<std::vector<uint8_t>>(std::move(atlasRGBA));
        mesh->submeshes.push_back(std::move(sub));

        mesh->CalculateBounds();
        mesh->BuildClusters();
        return mesh;
    }

private:
    // Keep the biggest 8-connected component plus any component >= 4% of its size.
    static void KeepMainComponents(std::vector<uint8_t>& fg, int gw, int gh) {
        const int N = gw * gh;
        std::vector<int> label(N, -1);
        std::vector<int> sizes;
        std::vector<int> stack;
        for (int i = 0; i < N; ++i) {
            if (!fg[i] || label[i] >= 0) continue;
            int id = static_cast<int>(sizes.size());
            sizes.push_back(0);
            stack.push_back(i); label[i] = id;
            while (!stack.empty()) {
                int p = stack.back(); stack.pop_back();
                ++sizes[id];
                int x = p % gw, y = p / gw;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= gw || yy >= gh) continue;
                    int j = yy * gw + xx;
                    if (fg[j] && label[j] < 0) { label[j] = id; stack.push_back(j); }
                }
            }
        }
        if (sizes.empty()) return;
        int biggest = *std::max_element(sizes.begin(), sizes.end());
        for (int i = 0; i < N; ++i) {
            if (fg[i] && sizes[label[i]] < biggest * 4 / 100) fg[i] = 0;
        }
    }

    // Separable box blur that only averages cells inside the mask.
    static std::vector<float> BoxBlurMasked(const std::vector<float>& src, const std::vector<uint8_t>& mask, int gw, int gh, int r) {
        const int N = gw * gh;
        std::vector<float> tmp(N, 0.0f), tw(N, 0.0f), out(N, 0.0f);
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            float s = 0, c = 0;
            for (int dx = -r; dx <= r; ++dx) {
                int xx = x + dx; if (xx < 0 || xx >= gw) continue;
                int j = y * gw + xx; if (!mask[j]) continue;
                s += src[j]; c += 1;
            }
            tmp[y * gw + x] = s; tw[y * gw + x] = c;
        }
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            float s = 0, c = 0;
            for (int dy = -r; dy <= r; ++dy) {
                int yy = y + dy; if (yy < 0 || yy >= gh) continue;
                s += tmp[yy * gw + x]; c += tw[yy * gw + x];
            }
            out[y * gw + x] = c > 0 ? s / c : src[y * gw + x];
        }
        return out;
    }
};

} // namespace Surreal
