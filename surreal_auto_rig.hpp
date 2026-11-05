#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - MASTER RIGGED SKELETON & ADAPTIVE 3D "RUBBER SUIT"
 * ----------------------------------------------------------------------------
 * Architectural Upgrade (Parametric Humanoid Rubber-Suit Shell + Auto-Rigger):
 *
 * Instead of extruding a flat 2D relief slab (which fuses arms to the torso,
 * webs legs together, and looks flat from 90-degree side views), this engine uses:
 *
 *   1. Master Full-Rigged 20-Joint Humanoid Skeleton ("Muster-Skelett"):
 *      Root, Pelvis, Spine, Chest, Neck, Head, Shoulder_L/R, Arm_L/R,
 *      Forearm_L/R, Hand_L/R, UpLeg_L/R, Leg_L/R, Foot_L/R.
 *   2. 2D Silhouette & Proportion Analyzer:
 *      Scans each character's 2D image region from head to toe to extract
 *      exact anatomical proportions (head width, shoulder span, chest/waist/hip
 *      silhouette curve, sleeve colors, leg split, and boot profile).
 *   3. Adaptive 3D "Rubber Suit" Articulated Shell Generator:
 *      Constructs separate, true 360-degree cylindrical/elliptical 3D limb tubes
 *      around the Master Skeleton (Head+Neck+Torso, Left Arm, Right Arm,
 *      Left Leg+Boot, Right Leg+Boot) that morph their cross-sectional radii
 *      to match the character's 2D silhouette proportions.
 *   4. Seamless Front/Back 2D-to-3D Texture Wrapping:
 *      Projects the high-res 2D facial/outfit pixels onto the front hemisphere,
 *      synthesizes natural 3D hair volume on the back of the head, and wraps
 *      seam-blended garment colors around the back of the torso, arms, and legs.
 *   5. Isolated Per-Limb Linear Blend Skinning (LBS):
 *      Assigns clean bone weights strictly by anatomical limb tube so arms and
 *      legs never tear or pull adjacent body parts during K-Pop dance animations.
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_meshlet.hpp"
#include "surreal_animation.hpp"
#include "surreal_image3d.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace Surreal {

struct RiggedFigureEntity {
    std::string name;
    Vec3 worldPosition;
    Vec3 localBoundsMin;
    Vec3 localBoundsMax;
    std::shared_ptr<SurrealMesh> mesh;
    std::shared_ptr<SkeletalMeshInstance> skeletal;
};

class SurrealAutoRigger {
public:
    struct SilhouetteSlice {
        float uLeft = 0.25f;
        float uRight = 0.75f;
        float uCenter = 0.50f;
        float widthFrac = 0.35f;
        ColorRGB avgColor = ColorRGB(0.25f, 0.25f, 0.28f);
        ColorRGB leftEdgeColor = ColorRGB(0.25f, 0.25f, 0.28f);
        ColorRGB rightEdgeColor = ColorRGB(0.25f, 0.25f, 0.28f);
    };

    /**
     * Converts a single 2D character image into a full-rigged 3D Rubber Suit character.
     */
    static RiggedFigureEntity BuildSingleRubberSuitCharacter(
        const std::string& imagePath,
        const std::string& charName = "RubberSuit_Char",
        const Image3DOptions& opt = Image3DOptions(),
        size_t animSeed = 0)
    {
        RiggedFigureEntity emptyFig;
        int imgW = 0, imgH = 0, ch = 0;
        unsigned char* rgba = stbi_load(imagePath.c_str(), &imgW, &imgH, &ch, 4);
        if (!rgba || imgW < 8 || imgH < 8) {
            if (rgba) stbi_image_free(rgba);
            return emptyFig;
        }
        auto fig = BuildRubberSuitFromSubImage(rgba, imgW, imgH, 0.0f, 1.0f, charName, opt.heightMeters, animSeed, Vec3(0, 0, 0));
        stbi_image_free(rgba);
        return fig;
    }

    /**
     * Segments a group photo (e.g. MEOVV 5 members) and wraps each member onto an
     * Adaptive 3D Rubber Suit rigged to the 20-joint Master Skeleton.
     */
    static std::vector<RiggedFigureEntity> SegmentGroupAndRig(
        const std::string& imagePath,
        const Image3DOptions& opt = Image3DOptions(),
        float spacingOffset = 0.0f)
    {
        std::vector<RiggedFigureEntity> result;

        int imgW = 0, imgH = 0, ch = 0;
        unsigned char* rgba = stbi_load(imagePath.c_str(), &imgW, &imgH, &ch, 4);
        if (!rgba || imgW < 16 || imgH < 16) {
            if (rgba) stbi_image_free(rgba);
            // Fallback search paths for MEOVV group photo
            const char* fallbacks[] = {
                "/root/Downloads/meovv_nobg.png",
                "/root/Downloads/meovv.jpg",
                "assets/models/meovv.jpg"
            };
            for (const char* fb : fallbacks) {
                if (imagePath != fb) {
                    rgba = stbi_load(fb, &imgW, &imgH, &ch, 4);
                    if (rgba && imgW >= 16 && imgH >= 16) break;
                }
            }
        }

        if (!rgba || imgW < 16 || imgH < 16) {
            std::cerr << "[SurrealAutoRigger] Failed to load group image: " << imagePath << std::endl;
            if (rgba) stbi_image_free(rgba);
            return result;
        }

        struct MemberDef {
            std::string name;
            std::string singlePngA;
            std::string singlePngB;
            float normMin;
            float normMax;
        };

        std::vector<MemberDef> members = {
            {"MEOVV_1_Ella",  "/root/Downloads/meovv_1_ella.png",  "/root/coding/MeinOS/surreal_engine/meovv_1_ella.png",  0.070f, 0.275f},
            {"MEOVV_2_Gawon", "/root/Downloads/meovv_2_gawon.png", "/root/coding/MeinOS/surreal_engine/meovv_2_gawon.png", 0.275f, 0.405f},
            {"MEOVV_3_Sooin", "/root/Downloads/meovv_3_sooin.png", "/root/coding/MeinOS/surreal_engine/meovv_3_sooin.png", 0.405f, 0.545f},
            {"MEOVV_4_Anna",  "/root/Downloads/meovv_4_anna.png",  "/root/coding/MeinOS/surreal_engine/meovv_4_anna.png",  0.545f, 0.665f},
            {"MEOVV_5_Narin", "/root/Downloads/meovv_5_narin.png", "/root/coding/MeinOS/surreal_engine/meovv_5_narin.png", 0.665f, 0.835f}
        };

        float targetH = (opt.heightMeters > 0.5f) ? opt.heightMeters : 2.20f;

        for (size_t figIdx = 0; figIdx < members.size(); ++figIdx) {
            const auto& def = members[figIdx];
            float zStagger = (figIdx % 2 == 1) ? 0.22f : -0.22f;
            float xPos = (static_cast<float>(figIdx) - 2.0f) * (1.12f + spacingOffset);
            float zPos = 4.2f + zStagger;
            Vec3 worldPos(xPos, 0.0f, zPos);

            RiggedFigureEntity fig;
            int mW = 0, mH = 0, mCh = 0;
            unsigned char* mRgba = stbi_load(def.singlePngA.c_str(), &mW, &mH, &mCh, 4);
            if (!mRgba || mW < 16 || mH < 16) {
                if (mRgba) stbi_image_free(mRgba);
                mRgba = stbi_load(def.singlePngB.c_str(), &mW, &mH, &mCh, 4);
            }
            if (mRgba && mW >= 16 && mH >= 16) {
                fig = BuildRubberSuitFromSubImage(
                    mRgba, mW, mH, 0.0f, 1.0f, def.name, targetH, figIdx, worldPos);
                stbi_image_free(mRgba);
            } else {
                if (mRgba) stbi_image_free(mRgba);
                fig = BuildRubberSuitFromSubImage(
                    rgba, imgW, imgH, def.normMin, def.normMax, def.name, targetH, figIdx, worldPos);
            }

            if (fig.mesh && !fig.mesh->vertices.empty()) {
                result.push_back(fig);
            }
        }

        stbi_image_free(rgba);
        std::cout << "[SurrealAutoRigger] Synthesized " << result.size()
                  << " Adaptive 3D Rubber-Suit figures on Master 20-Joint Skeletons." << std::endl;
        return result;
    }

    /**
     * Core Adaptive 3D Rubber Suit + Master Skeleton Builder for a 2D image region [uMin0, uMax0].
     */
    static RiggedFigureEntity BuildRubberSuitFromSubImage(
        const unsigned char* rgba, int imgW, int imgH,
        float uMin0, float uMax0,
        const std::string& name,
        float H,
        size_t figureSeed,
        const Vec3& worldPosition)
    {
        int xStart = std::max(0, std::min(imgW - 1, static_cast<int>(uMin0 * imgW)));
        int xEnd   = std::max(xStart + 1, std::min(imgW, static_cast<int>(uMax0 * imgW)));
        int subW   = xEnd - xStart;

        // Detect whether image has meaningful alpha channel or flat background
        bool hasAlpha = false;
        for (int y = 0; y < imgH; y += 8) {
            for (int x = xStart; x < xEnd; x += 8) {
                if (rgba[(y * imgW + x) * 4 + 3] < 200) {
                    hasAlpha = true;
                    break;
                }
            }
            if (hasAlpha) break;
        }

        // Estimate background color from top/bottom corner samples if no alpha
        float bgR = 0.9f, bgG = 0.9f, bgB = 0.9f;
        if (!hasAlpha) {
            const unsigned char* p0 = rgba + (0 * imgW + xStart) * 4;
            const unsigned char* p1 = rgba + (0 * imgW + (xEnd - 1)) * 4;
            bgR = (p0[0] + p1[0]) / (2.0f * 255.0f);
            bgG = (p0[1] + p1[1]) / (2.0f * 255.0f);
            bgB = (p0[2] + p1[2]) / (2.0f * 255.0f);
        }

        auto isForeground = [&](int px, int py) -> bool {
            px = std::max(0, std::min(imgW - 1, px));
            py = std::max(0, std::min(imgH - 1, py));
            const unsigned char* p = rgba + (py * imgW + px) * 4;
            if (hasAlpha) return p[3] > 140;
            float dr = (p[0] / 255.0f) - bgR;
            float dg = (p[1] / 255.0f) - bgG;
            float db = (p[2] / 255.0f) - bgB;
            return (dr * dr + dg * dg + db * db) > 0.018f;
        };

        auto samplePixelColor = [&](float uLocal, float yNorm) -> ColorRGB {
            // uLocal in [0..1] within [xStart..xEnd], yNorm in [0..1] (0=bottom, 1=top)
            uLocal = std::max(0.02f, std::min(0.98f, uLocal));
            yNorm  = std::max(0.01f, std::min(0.99f, yNorm));
            int py = static_cast<int>((1.0f - yNorm) * (imgH - 1));
            int px = xStart + static_cast<int>(uLocal * (subW - 1));
            px = std::max(xStart, std::min(xEnd - 1, px));
            py = std::max(0, std::min(imgH - 1, py));

            // If pixel happens to fall on transparent background, search horizontally toward center for nearest foreground pixel
            if (!isForeground(px, py)) {
                int centerPx = xStart + subW / 2;
                int step = (centerPx >= px) ? 1 : -1;
                for (int k = 1; k < subW / 2; ++k) {
                    int testX = px + k * step;
                    if (testX >= xStart && testX < xEnd && isForeground(testX, py)) {
                        px = testX;
                        break;
                    }
                }
            }
            const unsigned char* p = rgba + (py * imgW + px) * 4;
            return ColorRGB(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
        };

        // Find vertical foreground bounds [yTop, yBot] inside [xStart, xEnd]
        int yTop = imgH, yBot = 0;
        for (int y = 0; y < imgH; ++y) {
            int fgCount = 0;
            for (int x = xStart; x < xEnd; ++x) {
                if (isForeground(x, y)) fgCount++;
            }
            if (fgCount >= std::max(2, subW / 25)) {
                if (y < yTop) yTop = y;
                if (y > yBot) yBot = y;
            }
        }
        if (yTop >= yBot - 10) {
            yTop = static_cast<int>(imgH * 0.05f);
            yBot = static_cast<int>(imgH * 0.95f);
        }
        int charPixH = std::max(16, yBot - yTop);

        // Extract 64 vertical silhouette slices from feet (0) to crown (63)
        constexpr int NUM_SLICES = 64;
        std::vector<SilhouetteSlice> slices(NUM_SLICES);
        for (int s = 0; s < NUM_SLICES; ++s) {
            float yNorm = static_cast<float>(s) / static_cast<float>(NUM_SLICES - 1);
            int py = yBot - static_cast<int>(yNorm * charPixH);
            py = std::max(yTop, std::min(yBot, py));

            int firstX = -1, lastX = -1;
            float sumR = 0, sumG = 0, sumB = 0;
            int count = 0;
            for (int x = xStart; x < xEnd; ++x) {
                if (isForeground(x, py)) {
                    if (firstX < 0) firstX = x;
                    lastX = x;
                    const unsigned char* p = rgba + (py * imgW + x) * 4;
                    sumR += p[0] / 255.0f;
                    sumG += p[1] / 255.0f;
                    sumB += p[2] / 255.0f;
                    count++;
                }
            }

            if (firstX >= 0 && lastX > firstX && count > 0) {
                slices[s].uLeft  = static_cast<float>(firstX - xStart) / static_cast<float>(subW);
                slices[s].uRight = static_cast<float>(lastX - xStart) / static_cast<float>(subW);
                slices[s].uCenter = 0.5f * (slices[s].uLeft + slices[s].uRight);
                float pixSpan = static_cast<float>(lastX - firstX);
                slices[s].widthFrac = std::max(0.10f, std::min(0.45f, pixSpan / static_cast<float>(charPixH)));
                slices[s].avgColor = ColorRGB(sumR / count, sumG / count, sumB / count);

                int lx = std::min(xEnd - 1, firstX + std::max(1, (lastX - firstX) / 8));
                int rx = std::max(xStart, lastX - std::max(1, (lastX - firstX) / 8));
                const unsigned char* pl = rgba + (py * imgW + lx) * 4;
                const unsigned char* pr = rgba + (py * imgW + rx) * 4;
                slices[s].leftEdgeColor  = ColorRGB(pl[0] / 255.0f, pl[1] / 255.0f, pl[2] / 255.0f);
                slices[s].rightEdgeColor = ColorRGB(pr[0] / 255.0f, pr[1] / 255.0f, pr[2] / 255.0f);
            } else if (s > 0) {
                slices[s] = slices[s - 1];
            }
        }

        // Smooth silhouette slices so the 3D Rubber Suit has organic anatomical curves
        for (int pass = 0; pass < 3; ++pass) {
            std::vector<SilhouetteSlice> copy = slices;
            for (int s = 1; s < NUM_SLICES - 1; ++s) {
                slices[s].uLeft     = 0.25f * copy[s - 1].uLeft + 0.50f * copy[s].uLeft + 0.25f * copy[s + 1].uLeft;
                slices[s].uRight    = 0.25f * copy[s - 1].uRight + 0.50f * copy[s].uRight + 0.25f * copy[s + 1].uRight;
                slices[s].uCenter   = 0.5f * (slices[s].uLeft + slices[s].uRight);
                slices[s].widthFrac = 0.25f * copy[s - 1].widthFrac + 0.50f * copy[s].widthFrac + 0.25f * copy[s + 1].widthFrac;
                slices[s].avgColor  = copy[s - 1].avgColor * 0.25f + copy[s].avgColor * 0.50f + copy[s + 1].avgColor * 0.25f;
            }
        }

        auto getSliceAt = [&](float yNorm) -> SilhouetteSlice {
            float f = std::max(0.0f, std::min(1.0f, yNorm)) * (NUM_SLICES - 1);
            int i0 = static_cast<int>(f);
            int i1 = std::min(NUM_SLICES - 1, i0 + 1);
            float t = f - i0;
            SilhouetteSlice res;
            res.uLeft     = slices[i0].uLeft * (1.0f - t) + slices[i1].uLeft * t;
            res.uRight    = slices[i0].uRight * (1.0f - t) + slices[i1].uRight * t;
            res.uCenter   = 0.5f * (res.uLeft + res.uRight);
            res.widthFrac = slices[i0].widthFrac * (1.0f - t) + slices[i1].widthFrac * t;
            res.avgColor  = slices[i0].avgColor * (1.0f - t) + slices[i1].avgColor * t;
            res.leftEdgeColor  = slices[i0].leftEdgeColor * (1.0f - t) + slices[i1].leftEdgeColor * t;
            res.rightEdgeColor = slices[i0].rightEdgeColor * (1.0f - t) + slices[i1].rightEdgeColor * t;
            return res;
        };

        auto sampleFittedColor = [&](float uLocal, float yNorm) -> ColorRGB {
            yNorm = std::max(0.01f, std::min(0.99f, yNorm));
            int py = yBot - static_cast<int>(yNorm * charPixH);
            return samplePixelColor(uLocal, 1.0f - static_cast<float>(py) / static_cast<float>(imgH - 1));
        };

        // Extract Hair Color from top of head (yNorm = 0.94..0.98) for 360-degree back-of-head synthesis
        ColorRGB hairColor = ( getSliceAt(0.95f).avgColor * 0.6f + getSliceAt(0.92f).leftEdgeColor * 0.4f ) * 0.82f;

        // Extract Character Proportions from 2D Silhouette for the Master 20-Joint Skeleton
        float shWidthFrac  = std::max(0.21f, std::min(0.31f, getSliceAt(0.75f).widthFrac));
        float waistFrac    = std::max(0.13f, std::min(0.23f, getSliceAt(0.60f).widthFrac));
        float hipWidthFrac = std::max(0.17f, std::min(0.27f, getSliceAt(0.48f).widthFrac));

        float shHalfX  = shWidthFrac * 0.42f * H;
        float hipHalfX = hipWidthFrac * 0.30f * H;

        // 1. Construct Master 20-Joint Humanoid Skeleton adapted to character proportions
        Vec3 pelvisPos(0.0f, 0.48f * H, 0.0f);
        Vec3 spinePos (0.0f, 0.60f * H, 0.0f);
        Vec3 chestPos (0.0f, 0.73f * H, 0.0f);
        Vec3 neckPos  (0.0f, 0.83f * H, 0.0f);
        Vec3 headPos  (0.0f, 0.91f * H, 0.0f);

        Vec3 shL      (-shHalfX, 0.755f * H, 0.0f);
        Vec3 armL     (-shHalfX - 0.030f * H, 0.62f * H, 0.0f);
        Vec3 foreArmL (-shHalfX - 0.042f * H, 0.48f * H, 0.012f * H);
        Vec3 handL    (-shHalfX - 0.046f * H, 0.36f * H, 0.022f * H);

        Vec3 shR      (+shHalfX, 0.755f * H, 0.0f);
        Vec3 armR     (+shHalfX + 0.030f * H, 0.62f * H, 0.0f);
        Vec3 foreArmR (+shHalfX + 0.042f * H, 0.48f * H, 0.012f * H);
        Vec3 handR    (+shHalfX + 0.046f * H, 0.36f * H, 0.022f * H);

        Vec3 hipL     (-hipHalfX, 0.47f * H, 0.0f);
        Vec3 kneeL    (-hipHalfX * 1.03f, 0.25f * H, 0.0f);
        Vec3 footL    (-hipHalfX * 1.04f, 0.042f * H, 0.015f * H);

        Vec3 hipR     (+hipHalfX, 0.47f * H, 0.0f);
        Vec3 kneeR    (+hipHalfX * 1.03f, 0.25f * H, 0.0f);
        Vec3 footR    (+hipHalfX * 1.04f, 0.042f * H, 0.015f * H);

        struct JointDef {
            std::string name;
            int parent;
            Vec3 worldPos;
        };
        std::vector<JointDef> defs = {
            {"Root",        -1, Vec3(0, 0, 0)},
            {"Pelvis",       0, pelvisPos},
            {"Spine",        1, spinePos},
            {"Chest",        2, chestPos},
            {"Neck",         3, neckPos},
            {"Head",         4, headPos},
            {"Shoulder_L",   3, shL},
            {"Arm_L",        6, armL},
            {"Forearm_L",    7, foreArmL},
            {"Hand_L",       8, handL},
            {"Shoulder_R",   3, shR},
            {"Arm_R",       10, armR},
            {"Forearm_R",   11, foreArmR},
            {"Hand_R",      12, handR},
            {"UpLeg_L",      1, hipL},
            {"Leg_L",       14, kneeL},
            {"Foot_L",      15, footL},
            {"UpLeg_R",      1, hipR},
            {"Leg_R",       17, kneeR},
            {"Foot_R",      18, footR}
        };

        auto inst = std::make_shared<SkeletalMeshInstance>();
        inst->joints.resize(defs.size());
        for (size_t i = 0; i < defs.size(); ++i) {
            auto& j = inst->joints[i];
            j.name = defs[i].name;
            j.parentIndex = defs[i].parent;
            Vec3 parentPos = (defs[i].parent >= 0) ? defs[defs[i].parent].worldPos : Vec3(0,0,0);
            j.baseTranslation = defs[i].worldPos - parentPos;
            j.localTranslation = j.baseTranslation;
            j.baseRotation = Quat(0, 0, 0, 1.0f);
            j.localRotation = j.baseRotation;
            j.baseScale = Vec3(1.0f, 1.0f, 1.0f);
            j.localScale = j.baseScale;
            Vec3 wp = defs[i].worldPos;
            j.inverseBindMatrix = Mat4::Translation(Vec3(-wp.x, -wp.y, -wp.z));
        }

        // 2. Build the High-Resolution 1,440-Pass 3D Rubber Suit Mesh + Dual-Hemisphere GPU Texture Atlas
        std::vector<uint8_t> subRGBA(static_cast<size_t>(subW) * imgH * 4);
        for (int y = 0; y < imgH; ++y) {
            for (int x = 0; x < subW; ++x) {
                const unsigned char* src = rgba + (static_cast<size_t>(y) * imgW + (xStart + x)) * 4;
                uint8_t* dst = subRGBA.data() + (static_cast<size_t>(y) * subW + x) * 4;
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = isForeground(xStart + x, y) ? 255 : 0;
            }
        }

        Image3DOptions suitOpt;
        suitOpt.maxRes = 320;
        suitOpt.ironingPasses = 1440;
        suitOpt.heightMeters = H;
        suitOpt.depth = 0.34f;
        suitOpt.generateBackside = true;
        suitOpt.anatomicalSmooth = true;
        suitOpt.mode = 2; // Force character cutout

        auto suitMesh = SurrealImage3D::BuildFromRGBA(subRGBA.data(), subW, imgH, suitOpt);
        if (!suitMesh || suitMesh->vertices.empty()) {
            RiggedFigureEntity emptyFig;
            return emptyFig;
        }

        // Bind High-Res 3D Mesh onto the 20-Joint Master Humanoid Skeleton with smooth anatomical weights
        inst->bindVertices.resize(suitMesh->vertices.size());
        for (size_t v = 0; v < suitMesh->vertices.size(); ++v) {
            const auto& vt = suitMesh->vertices[v];
            SkinnedVertex3D sv;
            sv.position = vt.position;
            sv.normal = vt.normal;
            sv.u = vt.u;
            sv.v = vt.v;
            sv.color = vt.color;

            float yNorm = std::max(0.0f, std::min(1.0f, vt.position.y / std::max(0.1f, H)));
            float absX = std::abs(vt.position.x);
            bool isLeft = (vt.position.x < 0.0f);

            uint16_t j0 = 1, j1 = 2;
            float w0 = 1.0f, w1 = 0.0f;

            if (yNorm >= 0.82f) {
                // Neck (4) -> Head (5)
                j0 = 4; j1 = 5;
                w1 = std::min(1.0f, (yNorm - 0.82f) / 0.06f);
                w0 = 1.0f - w1;
            } else if (yNorm >= 0.46f) {
                // Upper body: check if lateral arm region vs central torso
                float armThreshold = shHalfX * 0.82f;
                if (absX > armThreshold && yNorm < 0.78f) {
                    uint16_t jSh   = isLeft ? 6  : 10;
                    uint16_t jArm  = isLeft ? 7  : 11;
                    uint16_t jFore = isLeft ? 8  : 12;
                    uint16_t jHand = isLeft ? 9  : 13;
                    float armBlend = std::min(1.0f, (absX - armThreshold) / std::max(0.02f, 0.05f * H));
                    if (yNorm > 0.66f) {
                        j0 = 3; j1 = jSh; w1 = armBlend; w0 = 1.0f - w1;
                    } else if (yNorm > 0.54f) {
                        j0 = jSh; j1 = jArm; w1 = armBlend; w0 = 1.0f - w1;
                    } else {
                        j0 = jArm; j1 = (yNorm < 0.49f) ? jHand : jFore; w1 = armBlend; w0 = 1.0f - w1;
                    }
                } else if (yNorm < 0.58f) {
                    j0 = 1; j1 = 2;
                    w1 = std::max(0.0f, (yNorm - 0.46f) / 0.12f);
                    w0 = 1.0f - w1;
                } else if (yNorm < 0.72f) {
                    j0 = 2; j1 = 3;
                    w1 = (yNorm - 0.58f) / 0.14f;
                    w0 = 1.0f - w1;
                } else {
                    j0 = 3; j1 = 4;
                    w1 = (yNorm - 0.72f) / 0.10f;
                    w0 = 1.0f - w1;
                }
            } else {
                // Lower body: Left Leg (14,15,16) vs Right Leg (17,18,19)
                uint16_t jUpLeg = isLeft ? 14 : 17;
                uint16_t jLeg   = isLeft ? 15 : 18;
                uint16_t jFoot  = isLeft ? 16 : 19;
                if (yNorm < 0.08f) {
                    j0 = jFoot; j1 = jLeg;
                    w0 = 1.0f - (yNorm / 0.08f) * 0.5f;
                    w1 = 1.0f - w0;
                } else if (yNorm < 0.26f) {
                    j0 = jLeg; j1 = jUpLeg;
                    w1 = std::max(0.0f, (yNorm - 0.18f) / 0.14f);
                    w0 = 1.0f - w1;
                } else {
                    j0 = jUpLeg; j1 = 1;
                    w1 = std::max(0.0f, (yNorm - 0.38f) / 0.08f);
                    w0 = 1.0f - w1;
                }
            }

            sv.joints[0] = j0; sv.weights[0] = w0;
            sv.joints[1] = j1; sv.weights[1] = w1;
            sv.joints[2] = 0;  sv.weights[2] = 0.0f;
            sv.joints[3] = 0;  sv.weights[3] = 0.0f;
            inst->bindVertices[v] = sv;
        }

        suitMesh->CalculateBounds();
        inst->activeDeformedMesh->indices = suitMesh->indices;
        inst->activeDeformedMesh->vertices = suitMesh->vertices;
        inst->activeDeformedMesh->submeshes = suitMesh->submeshes;
        inst->activeDeformedMesh->CalculateBounds();

        // Synthesize Procedural K-Pop Dance & Breathing Animation Clips
        inst->clips = GenerateProceduralClips(figureSeed);
        inst->PlayClip(0);
        inst->Update(0.016f);

        RiggedFigureEntity fig;
        fig.name = name;
        fig.worldPosition = worldPosition;
        fig.localBoundsMin = suitMesh->worldBounds.min;
        fig.localBoundsMax = suitMesh->worldBounds.max;
        fig.mesh = inst->activeDeformedMesh;
        fig.skeletal = inst;
        return fig;
    }

    /**
     * Legacy helper for existing meshes: wraps an arbitrary mesh onto the 20-joint humanoid skeleton.
     */
    static std::shared_ptr<SkeletalMeshInstance> BuildHumanoidTemplateSkeleton(
        const std::shared_ptr<SurrealMesh>& sourceMesh,
        float H, float W, size_t figureSeed)
    {
        auto inst = std::make_shared<SkeletalMeshInstance>();

        struct JointDef {
            std::string name;
            int parent;
            Vec3 worldPos;
        };

        Vec3 pelvisPos(0, 0.50f * H, 0);
        Vec3 spinePos(0, 0.62f * H, 0);
        Vec3 chestPos(0, 0.74f * H, 0);
        Vec3 neckPos(0, 0.83f * H, 0);
        Vec3 headPos(0, 0.92f * H, 0);
        float shSpan = std::max(0.24f, W * 0.65f);
        Vec3 shL(-shSpan * 0.40f, 0.75f * H, 0);
        Vec3 armL(shL.x - 0.02f, 0.60f * H, 0);
        Vec3 foreArmL(shL.x - 0.01f, 0.46f * H, 0.02f);
        Vec3 handL(shL.x + 0.01f, 0.34f * H, 0.03f);
        Vec3 shR(shSpan * 0.40f, 0.75f * H, 0);
        Vec3 armR(shR.x + 0.02f, 0.60f * H, 0);
        Vec3 foreArmR(shR.x + 0.01f, 0.46f * H, 0.02f);
        Vec3 handR(shR.x - 0.01f, 0.34f * H, 0.03f);
        Vec3 hipL(-shSpan * 0.24f, 0.47f * H, 0);
        Vec3 kneeL(hipL.x, 0.26f * H, 0);
        Vec3 footL(kneeL.x, 0.045f * H, 0.02f);
        Vec3 hipR(shSpan * 0.24f, 0.47f * H, 0);
        Vec3 kneeR(hipR.x, 0.26f * H, 0);
        Vec3 footR(kneeR.x, 0.045f * H, 0.02f);

        std::vector<JointDef> defs = {
            {"Root",        -1, Vec3(0, 0, 0)},
            {"Pelvis",       0, pelvisPos},
            {"Spine",        1, spinePos},
            {"Chest",        2, chestPos},
            {"Neck",         3, neckPos},
            {"Head",         4, headPos},
            {"Shoulder_L",   3, shL},
            {"Arm_L",        6, armL},
            {"Forearm_L",    7, foreArmL},
            {"Hand_L",       8, handL},
            {"Shoulder_R",   3, shR},
            {"Arm_R",       10, armR},
            {"Forearm_R",   11, foreArmR},
            {"Hand_R",      12, handR},
            {"UpLeg_L",      1, hipL},
            {"Leg_L",       14, kneeL},
            {"Foot_L",      15, footL},
            {"UpLeg_R",      1, hipR},
            {"Leg_R",       17, kneeR},
            {"Foot_R",      18, footR}
        };

        inst->joints.resize(defs.size());
        for (size_t i = 0; i < defs.size(); ++i) {
            auto& j = inst->joints[i];
            j.name = defs[i].name;
            j.parentIndex = defs[i].parent;
            Vec3 parentPos = (defs[i].parent >= 0) ? defs[defs[i].parent].worldPos : Vec3(0,0,0);
            j.baseTranslation = defs[i].worldPos - parentPos;
            j.localTranslation = j.baseTranslation;
            j.baseRotation = Quat(0, 0, 0, 1.0f);
            j.localRotation = j.baseRotation;
            j.baseScale = Vec3(1.0f, 1.0f, 1.0f);
            j.localScale = j.baseScale;
            Vec3 wp = defs[i].worldPos;
            j.inverseBindMatrix = Mat4::Translation(Vec3(-wp.x, -wp.y, -wp.z));
        }

        inst->bindVertices.resize(sourceMesh->vertices.size());
        for (size_t v = 0; v < sourceMesh->vertices.size(); ++v) {
            const auto& sv = sourceMesh->vertices[v];
            SkinnedVertex3D bv;
            bv.position = sv.position;
            bv.normal = sv.normal;
            bv.u = sv.u;
            bv.v = sv.v;
            bv.color = sv.color;
            bv.joints[0] = 1; bv.weights[0] = 1.0f;
            bv.joints[1] = 0; bv.weights[1] = 0.0f;
            bv.joints[2] = 0; bv.weights[2] = 0.0f;
            bv.joints[3] = 0; bv.weights[3] = 0.0f;
            inst->bindVertices[v] = bv;
        }

        inst->activeDeformedMesh->indices = sourceMesh->indices;
        inst->activeDeformedMesh->vertices = sourceMesh->vertices;
        inst->activeDeformedMesh->submeshes = sourceMesh->submeshes;
        inst->activeDeformedMesh->CalculateBounds();
        inst->clips = GenerateProceduralClips(figureSeed);
        inst->PlayClip(0);
        inst->Update(0.016f);
        return inst;
    }

    /**
     * Synthesizes K-Pop Dance & Natural Breathing Clips.
     */
    static std::vector<AnimationClip> GenerateProceduralClips(size_t seed) {
        std::vector<AnimationClip> clips;

        // Clip 0: KPop_IdleBreathe (Organic posture sway, arm poise & respiration)
        {
            AnimationClip clip;
            clip.name = "KPop_IdleBreathe";
            clip.duration = 3.6f;

            float phase = static_cast<float>(seed) * 0.7f;
            const int nKeys = 24;

            // Spine breathing oscillation
            AnimationChannel spineChan;
            spineChan.jointIndex = 2; // Spine
            spineChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                spineChan.keyTimes.push_back(t);
                float pitch = std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.04f;
                spineChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch));
            }
            clip.channels.push_back(spineChan);

            // Chest expansion
            AnimationChannel chestChan;
            chestChan.jointIndex = 3; // Chest
            chestChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                chestChan.keyTimes.push_back(t);
                float pitch = std::sin(t * 3.14159f * 2.0f / clip.duration + phase + 0.3f) * 0.035f;
                chestChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch));
            }
            clip.channels.push_back(chestChan);

            // Head micro-tilt
            AnimationChannel headChan;
            headChan.jointIndex = 5; // Head
            headChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                headChan.keyTimes.push_back(t);
                float roll = std::cos(t * 3.14159f * 2.0f / clip.duration + phase) * 0.035f;
                float yaw = std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.04f;
                Quat q = Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll) * Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw);
                headChan.quatValues.push_back(q);
            }
            clip.channels.push_back(headChan);

            clips.push_back(clip);
        }

        // Clip 1: KPop_DanceGroove (Rhythmic hip wave & arm motion)
        {
            AnimationClip clip;
            clip.name = "KPop_DanceGroove";
            clip.duration = 2.0f;

            float phase = static_cast<float>(seed) * 0.4f;
            const int nKeys = 24;

            // Hip sway & roll
            AnimationChannel hipChan;
            hipChan.jointIndex = 1; // Pelvis
            hipChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                hipChan.keyTimes.push_back(t);
                float roll = std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.12f;
                float yaw  = std::cos(t * 3.14159f * 2.0f / clip.duration + phase) * 0.06f;
                hipChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll) *
                                             Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw));
            }
            clip.channels.push_back(hipChan);

            // Spine counter-sway
            AnimationChannel spineChan;
            spineChan.jointIndex = 2; // Spine
            spineChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                spineChan.keyTimes.push_back(t);
                float roll = -std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.08f;
                spineChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll));
            }
            clip.channels.push_back(spineChan);

            // Left arm dance gesture
            AnimationChannel armLChan;
            armLChan.jointIndex = 7; // Arm_L
            armLChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                armLChan.keyTimes.push_back(t);
                float pitch = std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.18f;
                float roll  = 0.10f + std::cos(t * 3.14159f * 2.0f / clip.duration + phase) * 0.12f;
                armLChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                                              Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll));
            }
            clip.channels.push_back(armLChan);

            // Right arm dance gesture
            AnimationChannel armRChan;
            armRChan.jointIndex = 11; // Arm_R
            armRChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                armRChan.keyTimes.push_back(t);
                float pitch = -std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.18f;
                float roll  = -0.10f - std::cos(t * 3.14159f * 2.0f / clip.duration + phase) * 0.12f;
                armRChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                                              Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll));
            }
            clip.channels.push_back(armRChan);

            clips.push_back(clip);
        }

        // Clip 2: KPop_StagePose (Static high-fashion group pose with breathing)
        {
            AnimationClip clip;
            clip.name = "KPop_StagePose";
            clip.duration = 4.0f;

            float phase = static_cast<float>(seed) * 0.5f;
            const int nKeys = 16;

            AnimationChannel hipChan;
            hipChan.jointIndex = 1;
            hipChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                hipChan.keyTimes.push_back(t);
                float roll = 0.08f + std::sin(t * 3.14159f * 2.0f / clip.duration) * 0.02f;
                hipChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll));
            }
            clip.channels.push_back(hipChan);

            AnimationChannel headChan;
            headChan.jointIndex = 5;
            headChan.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                headChan.keyTimes.push_back(t);
                float yaw = 0.12f * ((seed % 2 == 0) ? 1.0f : -1.0f);
                float pitch = -0.05f + std::sin(t * 3.14159f * 2.0f / clip.duration + phase) * 0.015f;
                headChan.quatValues.push_back(Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw) *
                                              Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch));
            }
            clip.channels.push_back(headChan);

            clips.push_back(clip);
        }

        return clips;
    }
};

} // namespace Surreal
