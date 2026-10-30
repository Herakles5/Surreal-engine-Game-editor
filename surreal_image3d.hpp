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
    int   maxRes       = 240;   // grid cells along the longest picture side
    float heightMeters = 1.80f; // world height of the resulting model
    float depth        = 0.85f; // half thickness relative to the inflate radius
    float relief       = 0.18f; // strength of luminance detail on the front
    float backBlend    = 0.35f; // 0 = back is a blurred copy, 1 = flat average colour
    float backDarken   = 0.85f; // brightness of the (guessed) back side
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
            inf.error = "Bild konnte nicht geladen werden: " + path;
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

        // ---- 1. Downsample to the working grid (box filter, alpha weighted) ----
        float sc = std::min(1.0f, static_cast<float>(opt.maxRes) / static_cast<float>(std::max(w, h)));
        int gw = std::max(2, static_cast<int>(std::lround(w * sc)));
        int gh = std::max(2, static_cast<int>(std::lround(h * sc)));
        const int N = gw * gh;
        inf.gridW = gw; inf.gridH = gh;

        std::vector<float> R(N), G(N), B(N), A(N);
        for (int gy = 0; gy < gh; ++gy) {
            int y0 = static_cast<int>((static_cast<int64_t>(gy) * h) / gh);
            int y1 = std::max(y0 + 1, static_cast<int>((static_cast<int64_t>(gy + 1) * h) / gh));
            for (int gx = 0; gx < gw; ++gx) {
                int x0 = static_cast<int>((static_cast<int64_t>(gx) * w) / gw);
                int x1 = std::max(x0 + 1, static_cast<int>((static_cast<int64_t>(gx + 1) * w) / gw));
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

        if (transparent > N / 100) {
            inf.usedAlpha = true;
            for (int i = 0; i < N; ++i) fg[i] = (A[i] >= 0.5f) ? 1 : 0;
        } else {
            // Background colour = per-channel median of the picture border
            std::vector<float> br, bg, bb;
            auto addBorder = [&](int x, int y) { int i = y * gw + x; br.push_back(R[i]); bg.push_back(G[i]); bb.push_back(B[i]); };
            for (int x = 0; x < gw; ++x) { addBorder(x, 0); addBorder(x, gh - 1); }
            for (int y = 1; y < gh - 1; ++y) { addBorder(0, y); addBorder(gw - 1, y); }
            auto median = [](std::vector<float>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
            float mr = median(br), mg = median(bg), mb = median(bb);
            inf.bgR = mr; inf.bgG = mg; inf.bgB = mb;

            auto distBg = [&](int i) {
                float dr = R[i] - mr, dg = G[i] - mg, db = B[i] - mb;
                return std::sqrt(dr * dr + dg * dg + db * db);
            };
            std::vector<float> bd;
            for (int x = 0; x < gw; ++x) { bd.push_back(distBg(x)); bd.push_back(distBg((gh - 1) * gw + x)); }
            for (int y = 1; y < gh - 1; ++y) { bd.push_back(distBg(y * gw)); bd.push_back(distBg(y * gw + gw - 1)); }
            std::sort(bd.begin(), bd.end());
            float p90 = bd[static_cast<size_t>(bd.size() * 0.9f)];
            float thr = std::min(80.0f, std::max(22.0f, p90 * 1.4f + 14.0f));

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
        }

        int fgCount = 0;
        for (int i = 0; i < N; ++i) fgCount += fg[i];
        if (fgCount > N * 92 / 100) {
            // Nothing to cut out (landscape, photo, texture): make a thick plate with relief.
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
            inf.error = "Kein Objekt im Bild erkannt (Hintergrund nicht einfarbig?)";
            return nullptr;
        }

        auto isFg = [&](int x, int y) { return x >= 0 && y >= 0 && x < gw && y < gh && fg[y * gw + x]; };

        // Edge colour decontamination: rim cells often blend with the background colour,
        // so they take the colour of their interior neighbours instead.
        if (!inf.plateMode) {
            std::vector<float> nR = R, nG = G, nB = B;
            for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
                if (!isFg(x, y)) continue;
                bool boundary = !isFg(x - 1, y) || !isFg(x + 1, y) || !isFg(x, y - 1) || !isFg(x, y + 1);
                if (!boundary) continue;
                float sr = 0, sg = 0, sb = 0; int c = 0;
                for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                    int xx = x + dx, yy = y + dy;
                    if (!isFg(xx, yy)) continue;
                    bool nb = !isFg(xx - 1, yy) || !isFg(xx + 1, yy) || !isFg(xx, yy - 1) || !isFg(xx, yy + 1);
                    if (nb) continue;
                    int j = yy * gw + xx; sr += R[j]; sg += G[j]; sb += B[j]; ++c;
                }
                if (c) { int i = y * gw + x; nR[i] = sr / c; nG[i] = sg / c; nB[i] = sb / c; }
            }
            R.swap(nR); G.swap(nG); B.swap(nB);
        }

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

        // ---- 4. Thickness profile + luminance relief ----
        float Rinfl = inf.plateMode ? std::max(2.5f, 0.07f * std::min(gw, gh)) : dmax;
        float Hmax  = inf.plateMode ? 0.05f * std::min(gw, gh) : opt.depth * Rinfl;

        std::vector<float> hh(N, 0.0f), zf(N, 0.0f);
        std::vector<float> lum(N, 0.0f);
        for (int i = 0; i < N; ++i) lum[i] = (0.299f * R[i] + 0.587f * G[i] + 0.114f * B[i]) / 255.0f;
        std::vector<float> lumBlur = BoxBlurMasked(lum, fg, gw, gh, inf.plateMode ? 6 : 3);
        float reliefAmp = opt.relief * Hmax * (inf.plateMode ? 3.0f : 1.0f);

        for (int i = 0; i < N; ++i) {
            if (!fg[i]) continue;
            float t = std::min(1.0f, dEff[i] / Rinfl);
            hh[i] = Hmax * std::sqrt(std::max(0.0f, 1.0f - (1.0f - t) * (1.0f - t)));
            float detail = std::max(-1.0f, std::min(1.0f, (lum[i] - lumBlur[i]) * 4.0f));
            float edgeFade = std::min(1.0f, dEff[i] / 3.0f);
            zf[i] = std::max(0.0f, hh[i] + reliefAmp * detail * edgeFade);
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

        // ---- 6. Colours: front = picture, back = blurred + averaged + darker ----
        float avR = 0, avG = 0, avB = 0;
        for (int i = 0; i < N; ++i) if (fg[i]) { avR += R[i]; avG += G[i]; avB += B[i]; }
        avR /= fgCount; avG /= fgCount; avB /= fgCount;
        std::vector<float> bR = BoxBlurMasked(R, fg, gw, gh, 5);
        std::vector<float> bG = BoxBlurMasked(G, fg, gw, gh, 5);
        std::vector<float> bB = BoxBlurMasked(B, fg, gw, gh, 5);

        auto HH = [&](const std::vector<float>& f, int x, int y) { return isFg(x, y) ? f[y * gw + x] : 0.0f; };

        auto mesh = std::make_shared<SurrealMesh>();
        std::vector<int> idxF(N, -1), idxB(N, -1);
        mesh->vertices.reserve(static_cast<size_t>(fgCount) * 2);

        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            int i = y * gw + x;
            if (!fg[i]) continue;
            float px = (x - cx) * s;
            float py = (maxy - y) * s;
            float u = (x + 0.5f) / gw, v = (y + 0.5f) / gh;

            // front
            float fx = (HH(zf, x + 1, y) - HH(zf, x - 1, y)) * 0.5f;
            float fyw = (HH(zf, x, y + 1) - HH(zf, x, y - 1)) * 0.5f;
            Vec3 nF = Vec3(-fx, fyw, 1.0f).Normalized();
            Vec4 cF(R[i] / 255.0f, G[i] / 255.0f, B[i] / 255.0f, 1.0f);
            idxF[i] = static_cast<int>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(Vec3(px, py, zf[i] * s), nF, u, v, cF));

            // back (mirror of the silhouette)
            float bx = (HH(hh, x + 1, y) - HH(hh, x - 1, y)) * 0.5f;
            float byw = (HH(hh, x, y + 1) - HH(hh, x, y - 1)) * 0.5f;
            Vec3 nB = Vec3(-bx, byw, -1.0f).Normalized();
            float k = opt.backBlend;
            float cr = (bR[i] * (1 - k) + avR * k) * opt.backDarken / 255.0f;
            float cg = (bG[i] * (1 - k) + avG * k) * opt.backDarken / 255.0f;
            float cb = (bB[i] * (1 - k) + avB * k) * opt.backDarken / 255.0f;
            idxB[i] = static_cast<int>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(Vec3(px, py, -hh[i] * s), nB, 1.0f - u, v, Vec4(cr, cg, cb, 1.0f)));
        }

        // ---- 7. Triangles: every fully covered cell -> front quad + back quad ----
        mesh->indices.reserve(static_cast<size_t>(fgCount) * 12);
        for (int y = 0; y + 1 < gh; ++y) for (int x = 0; x + 1 < gw; ++x) {
            int a = y * gw + x, b = a + 1, c = a + gw + 1, d = a + gw;   // A top-left, B top-right, C bottom-right, D bottom-left
            if (!(fg[a] && fg[b] && fg[c] && fg[d])) continue;
            // front (CCW seen from +Z)
            mesh->indices.push_back(idxF[a]); mesh->indices.push_back(idxF[d]); mesh->indices.push_back(idxF[c]);
            mesh->indices.push_back(idxF[a]); mesh->indices.push_back(idxF[c]); mesh->indices.push_back(idxF[b]);
        }
        for (int y = 0; y + 1 < gh; ++y) for (int x = 0; x + 1 < gw; ++x) {
            int a = y * gw + x, b = a + 1, c = a + gw + 1, d = a + gw;
            if (!(fg[a] && fg[b] && fg[c] && fg[d])) continue;
            // back (CCW seen from -Z)
            mesh->indices.push_back(idxB[a]); mesh->indices.push_back(idxB[c]); mesh->indices.push_back(idxB[d]);
            mesh->indices.push_back(idxB[a]); mesh->indices.push_back(idxB[b]); mesh->indices.push_back(idxB[c]);
        }
        if (mesh->indices.size() < 3) {
            inf.error = "Objekt zu duenn / zu klein fuer ein Mesh";
            return nullptr;
        }

        // ---- 8. Smooth Phong Normals (Area-Weighted Normal Accumulation) ----
        // Eliminates polygon faceting across front, back, and silhouette curves
        std::vector<Vec3> accumNormals(mesh->vertices.size(), Vec3(0, 0, 0));
        for (size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
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
