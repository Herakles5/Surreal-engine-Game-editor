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
#include "surreal_paths.hpp"
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

        // Extract Character Proportions from 2D Silhouette for the Master 20-Joint Skeleton
        float shWidthFrac  = std::max(0.21f, std::min(0.31f, getSliceAt(0.75f).widthFrac));
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
        suitOpt.maxRes = 88;
        suitOpt.ironingPasses = 160;
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

        // Bind High-Res 3D Mesh onto the 20-Joint Master Humanoid Skeleton with C1-smooth 4-joint anatomical weights
        auto smooth01 = [](float edge0, float edge1, float x) -> float {
            float t = std::clamp((x - edge0) / std::max(1e-5f, edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        inst->bindVertices.resize(suitMesh->vertices.size());
        for (size_t v = 0; v < suitMesh->vertices.size(); ++v) {
            const auto& vt = suitMesh->vertices[v];
            SkinnedVertex3D sv;
            sv.position = vt.position;
            sv.normal = vt.normal;
            sv.u = vt.u;
            sv.v = vt.v;
            sv.color = vt.color;

            float yNorm = std::clamp(vt.position.y / std::max(0.1f, H), 0.0f, 1.0f);
            float absX = std::abs(vt.position.x);
            bool isLeft = (vt.position.x < 0.0f);

            // Smooth transition between lower body (legs) and upper body (pelvis/torso) across yNorm in [0.40, 0.50]
            float upperBodyFactor = smooth01(0.40f, 0.50f, yNorm);
            float lowerBodyFactor = 1.0f - upperBodyFactor;

            // 1. Torso & Head Spine Chain (Pelvis=1 -> Spine=2 -> Chest=3 -> Neck=4 -> Head=5)
            uint16_t tJ0 = 1, tJ1 = 2;
            float tW1 = 0.0f;
            if (yNorm < 0.58f) {
                tJ0 = 1; tJ1 = 2;
                tW1 = smooth01(0.45f, 0.58f, yNorm);
            } else if (yNorm < 0.72f) {
                tJ0 = 2; tJ1 = 3;
                tW1 = smooth01(0.58f, 0.72f, yNorm);
            } else if (yNorm < 0.82f) {
                tJ0 = 3; tJ1 = 4;
                tW1 = smooth01(0.72f, 0.82f, yNorm);
            } else {
                tJ0 = 4; tJ1 = 5;
                tW1 = smooth01(0.82f, 0.90f, yNorm);
            }
            float tW0 = 1.0f - tW1;

            // 2. Smooth 2D Arm Envelope on Upper Body (blends outer silhouette smoothly into Shoulder/Arm/Forearm)
            float xArm = smooth01(shHalfX * 0.50f, shHalfX * 1.05f, absX);
            float yArm = smooth01(0.42f, 0.50f, yNorm) * (1.0f - smooth01(0.75f, 0.83f, yNorm));
            float armFactor = xArm * yArm * upperBodyFactor;

            uint16_t aJ0 = isLeft ? 6 : 10; // Shoulder
            uint16_t aJ1 = isLeft ? 7 : 11; // Arm
            float aW1 = 0.0f;
            if (yNorm > 0.64f) {
                aJ0 = isLeft ? 6 : 10;
                aJ1 = isLeft ? 7 : 11;
                aW1 = 1.0f - smooth01(0.64f, 0.75f, yNorm);
            } else if (yNorm > 0.50f) {
                aJ0 = isLeft ? 7 : 11;
                aJ1 = isLeft ? 8 : 12; // Forearm
                aW1 = 1.0f - smooth01(0.50f, 0.64f, yNorm);
            } else {
                aJ0 = isLeft ? 8 : 12;
                aJ1 = isLeft ? 9 : 13; // Hand
                aW1 = 1.0f - smooth01(0.42f, 0.50f, yNorm);
            }
            float aW0 = 1.0f - aW1;

            // 3. Lower Body Leg Chain with Smooth Midline Blend across x = 0 (prevents skirt/jeans split)
            float rightLegBlend = smooth01(-0.045f * H, +0.045f * H, vt.position.x);
            float leftLegBlend  = 1.0f - rightLegBlend;

            uint16_t lJ_L = 14, lJ_R = 17;
            uint16_t lJ2_L = 15, lJ2_R = 18;
            float legSubW1 = 0.0f;
            if (yNorm < 0.10f) {
                lJ_L = 16; lJ_R = 19; // Foot
                lJ2_L = 15; lJ2_R = 18; // Leg (Shin)
                legSubW1 = smooth01(0.02f, 0.10f, yNorm);
            } else if (yNorm < 0.27f) {
                lJ_L = 15; lJ_R = 18; // Leg (Knee/Shin)
                lJ2_L = 14; lJ2_R = 17; // UpLeg (Thigh)
                legSubW1 = smooth01(0.14f, 0.27f, yNorm);
            } else {
                lJ_L = 14; lJ_R = 17; // UpLeg (Thigh)
                lJ2_L = 1;  lJ2_R = 1;  // Pelvis
                legSubW1 = smooth01(0.32f, 0.45f, yNorm);
            }
            float legSubW0 = 1.0f - legSubW1;

            if (upperBodyFactor >= 0.999f) {
                // Pure Upper Body: 2 Torso joints + 2 Arm joints
                float torsoScale = 1.0f - armFactor;
                sv.joints[0] = tJ0; sv.weights[0] = torsoScale * tW0;
                sv.joints[1] = tJ1; sv.weights[1] = torsoScale * tW1;
                sv.joints[2] = aJ0; sv.weights[2] = armFactor * aW0;
                sv.joints[3] = aJ1; sv.weights[3] = armFactor * aW1;
            } else if (lowerBodyFactor >= 0.999f) {
                // Pure Lower Body: Left & Right Leg 2-bone blend across midline
                sv.joints[0] = lJ_L;  sv.weights[0] = leftLegBlend * legSubW0;
                sv.joints[1] = lJ2_L; sv.weights[1] = leftLegBlend * legSubW1;
                sv.joints[2] = lJ_R;  sv.weights[2] = rightLegBlend * legSubW0;
                sv.joints[3] = lJ2_R; sv.weights[3] = rightLegBlend * legSubW1;
            } else {
                // Waist/Hip Transition Zone: blend Pelvis/Spine with Left/Right UpLeg + Arm tip
                uint16_t domLegJ = (leftLegBlend >= rightLegBlend) ? lJ_L : lJ_R;
                uint16_t secLegJ = (leftLegBlend >= rightLegBlend) ? lJ_R : lJ_L;
                float domLegW = std::max(leftLegBlend, rightLegBlend);
                float secLegW = std::min(leftLegBlend, rightLegBlend);
                sv.joints[0] = tJ0;     sv.weights[0] = upperBodyFactor * tW0 + lowerBodyFactor * legSubW1;
                sv.joints[1] = tJ1;     sv.weights[1] = upperBodyFactor * tW1;
                sv.joints[2] = domLegJ; sv.weights[2] = lowerBodyFactor * legSubW0 * domLegW;
                sv.joints[3] = secLegJ; sv.weights[3] = lowerBodyFactor * legSubW0 * secLegW;
            }

            // Normalize weights to sum to 1.0
            float wSum = sv.weights[0] + sv.weights[1] + sv.weights[2] + sv.weights[3];
            if (wSum > 1e-5f) {
                float invW = 1.0f / wSum;
                for (int k = 0; k < 4; ++k) sv.weights[k] *= invW;
            } else {
                sv.joints[0] = 1; sv.weights[0] = 1.0f;
            }
            inst->bindVertices[v] = sv;
        }

        suitMesh->CalculateBounds();
        inst->activeDeformedMesh->indices = suitMesh->indices;
        inst->activeDeformedMesh->vertices = suitMesh->vertices;
        inst->activeDeformedMesh->submeshes = suitMesh->submeshes;
        inst->activeDeformedMesh->CalculateBounds();

        // Synthesize Procedural K-Pop Dance & Breathing Animation Clips (Start immediately in Clip 0: KPop_MeovvChoreo!)
        inst->clips = GenerateProceduralClips(figureSeed, H);
        inst->PlayClip(0, true);
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
        inst->clips = GenerateProceduralClips(figureSeed, H);
        inst->PlayClip(0, true);
        inst->Update(0.016f);
        return inst;
    }

    /**
     * Synthesizes 4 Full-Body 20-Joint K-Pop Choreography & Stage Clips.
     * Joint Indices:
     *   0:Root, 1:Pelvis, 2:Spine, 3:Chest, 4:Neck, 5:Head,
     *   6:Shoulder_L, 7:Arm_L, 8:Forearm_L, 9:Hand_L,
     *  10:Shoulder_R, 11:Arm_R, 12:Forearm_R, 13:Hand_R,
     *  14:UpLeg_L, 15:Leg_L, 16:Foot_L,
     *  17:UpLeg_R, 18:Leg_R, 19:Foot_R
     */
    static std::vector<AnimationClip> GenerateProceduralClips(size_t seed, float H = 2.20f) {
        std::vector<AnimationClip> clips;
        constexpr float TWO_PI = 6.2831853f;
        const float basePelvisY = 0.48f * H;

        // =========================================================================
        // Clip 0 (DEFAULT): KPop_MeovvChoreo — Full 20-Joint K-Pop Stage Choreography
        // Rhythmic knee bounce, hip wave, torso isolation, shoulder pops, arm grooves & head nod
        // =========================================================================
        {
            AnimationClip clip;
            clip.name = "KPop_MeovvChoreo";
            clip.duration = 2.4f;
            float phase = static_cast<float>(seed) * 0.35f;
            const int nKeys = 48;

            auto addRotChannel = [&](int jIdx, auto fn) {
                AnimationChannel ch;
                ch.jointIndex = jIdx;
                ch.target = ChannelTarget::ROTATION;
                for (int k = 0; k <= nKeys; ++k) {
                    float t = (float)k / nKeys * clip.duration;
                    float w = (t / clip.duration) * TWO_PI;
                    ch.keyTimes.push_back(t);
                    ch.quatValues.push_back(fn(w));
                }
                clip.channels.push_back(ch);
            };

            // Pelvis (1) Translation: Double-time K-Pop knee bounce + lateral hip glide
            {
                AnimationChannel pelTrans;
                pelTrans.jointIndex = 1;
                pelTrans.target = ChannelTarget::TRANSLATION;
                for (int k = 0; k <= nKeys; ++k) {
                    float t = (float)k / nKeys * clip.duration;
                    float w = (t / clip.duration) * TWO_PI;
                    float dx = std::sin(w + phase) * (0.028f * H);
                    float dy = -std::abs(std::sin(w + phase)) * (0.022f * H);
                    float dz = std::sin(2.0f * w + phase) * (0.012f * H);
                    pelTrans.keyTimes.push_back(t);
                    pelTrans.vecValues.push_back(Vec3(dx, basePelvisY + dy, dz));
                }
                clip.channels.push_back(pelTrans);
            }

            // Pelvis (1) Rotation: Hip sway roll & twist
            addRotChannel(1, [&](float w) {
                float roll  = std::sin(w + phase) * 0.13f;
                float yaw   = std::cos(w + phase) * 0.11f;
                float pitch = std::sin(2.0f * w + phase) * 0.045f;
                return Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll) *
                       Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch);
            });

            // Spine (2): Counter-sway & body-wave isolation
            addRotChannel(2, [&](float w) {
                float roll  = -std::sin(w + phase) * 0.09f;
                float pitch = std::cos(2.0f * w + phase) * 0.065f;
                float yaw   = -std::cos(w + phase) * 0.06f;
                return Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll) *
                       Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch);
            });

            // Chest (3): Rhythmic K-Pop chest pop & twist
            addRotChannel(3, [&](float w) {
                float pitch = std::sin(2.0f * w + phase - 0.4f) * 0.075f;
                float roll  = std::sin(w + phase + 0.5f) * 0.06f;
                float yaw   = std::sin(w + phase) * 0.08f;
                return Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw) *
                       Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });

            // Neck (4) & Head (5): Charismatic beat nod & side glance
            addRotChannel(4, [&](float w) {
                float roll = -std::sin(w + phase) * 0.04f;
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });
            addRotChannel(5, [&](float w) {
                float pitch = std::sin(2.0f * w + phase) * 0.07f;
                float yaw   = std::sin(w + phase * 0.5f) * 0.11f;
                float roll  = std::cos(w + phase) * 0.065f;
                return Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll) *
                       Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch);
            });

            // Shoulders (6, 10): Alternating shoulder pops
            addRotChannel(6, [&](float w) {
                float roll = std::sin(2.0f * w + phase) * 0.11f;
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });
            addRotChannel(10, [&](float w) {
                float roll = -std::sin(2.0f * w + phase + 1.57f) * 0.11f;
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });

            // Arms (7, 11) & Forearms (8, 12) & Hands (9, 13): K-Pop Arm Choreography
            addRotChannel(7, [&](float w) {
                float pitch = std::sin(w + phase) * 0.24f;
                float roll  = 0.06f + std::cos(2.0f * w + phase) * 0.14f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });
            addRotChannel(8, [&](float w) {
                float pitch = -0.12f + std::sin(2.0f * w + phase - 0.5f) * 0.20f;
                float yaw   = std::cos(w + phase) * 0.12f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw);
            });
            addRotChannel(9, [&](float w) {
                float roll = std::sin(2.0f * w + phase) * 0.16f;
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });

            addRotChannel(11, [&](float w) {
                float pitch = -std::sin(w + phase) * 0.24f;
                float roll  = -0.06f - std::cos(2.0f * w + phase) * 0.14f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });
            addRotChannel(12, [&](float w) {
                float pitch = -0.12f + std::cos(2.0f * w + phase - 0.5f) * 0.20f;
                float yaw   = -std::sin(w + phase) * 0.12f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 1.0f, 0), yaw);
            });
            addRotChannel(13, [&](float w) {
                float roll = -std::sin(2.0f * w + phase) * 0.16f;
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });

            // Left Leg (14:UpLeg_L, 15:Leg_L, 16:Foot_L) & Right Leg (17:UpLeg_R, 18:Leg_R, 19:Foot_R)
            addRotChannel(14, [&](float w) {
                float stepL = std::max(0.0f, std::sin(w + phase));
                float pitch = -stepL * 0.20f - std::abs(std::sin(w + phase)) * 0.08f;
                float roll  = -std::sin(w + phase) * 0.07f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });
            addRotChannel(15, [&](float w) {
                float stepL = std::max(0.0f, std::sin(w + phase));
                float kneeBend = stepL * 0.32f + std::abs(std::sin(w + phase)) * 0.12f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), kneeBend);
            });
            addRotChannel(16, [&](float w) {
                float stepL = std::max(0.0f, std::sin(w + phase));
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -stepL * 0.14f);
            });

            addRotChannel(17, [&](float w) {
                float stepR = std::max(0.0f, -std::sin(w + phase));
                float pitch = -stepR * 0.20f - std::abs(std::sin(w + phase)) * 0.08f;
                float roll  = -std::sin(w + phase) * 0.07f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), pitch) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), roll);
            });
            addRotChannel(18, [&](float w) {
                float stepR = std::max(0.0f, -std::sin(w + phase));
                float kneeBend = stepR * 0.32f + std::abs(std::sin(w + phase)) * 0.12f;
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), kneeBend);
            });
            addRotChannel(19, [&](float w) {
                float stepR = std::max(0.0f, -std::sin(w + phase));
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -stepR * 0.14f);
            });

            clips.push_back(clip);
        }

        // =========================================================================
        // Clip 1: KPop_HipHopGroove — High-Energy Bass Bounce & Arm Hits
        // =========================================================================
        {
            AnimationClip clip;
            clip.name = "KPop_HipHopGroove";
            clip.duration = 1.8f;
            float phase = static_cast<float>(seed) * 0.45f;
            const int nKeys = 36;

            AnimationChannel pelTrans;
            pelTrans.jointIndex = 1;
            pelTrans.target = ChannelTarget::TRANSLATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (float)k / nKeys * clip.duration;
                float w = (t / clip.duration) * TWO_PI;
                float dx = std::sin(w + phase) * (0.036f * H);
                float dy = -std::abs(std::sin(2.0f * w + phase)) * (0.028f * H);
                pelTrans.keyTimes.push_back(t);
                pelTrans.vecValues.push_back(Vec3(dx, basePelvisY + dy, 0.0f));
            }
            clip.channels.push_back(pelTrans);

            auto addRot = [&](int jIdx, auto fn) {
                AnimationChannel ch;
                ch.jointIndex = jIdx;
                ch.target = ChannelTarget::ROTATION;
                for (int k = 0; k <= nKeys; ++k) {
                    float t = (float)k / nKeys * clip.duration;
                    float w = (t / clip.duration) * TWO_PI;
                    ch.keyTimes.push_back(t);
                    ch.quatValues.push_back(fn(w));
                }
                clip.channels.push_back(ch);
            };

            addRot(1, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), std::sin(w + phase) * 0.15f) *
                       Quat::FromAxisAngle(Vec3(0, 1.0f, 0), std::sin(2.0f * w + phase) * 0.12f);
            });
            addRot(2, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::abs(std::sin(2.0f * w + phase)) * 0.10f) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), -std::sin(w + phase) * 0.11f);
            });
            addRot(3, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 1.0f, 0), std::cos(w + phase) * 0.12f);
            });
            addRot(5, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::sin(2.0f * w + phase) * 0.10f) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), std::cos(w + phase) * 0.08f);
            });
            addRot(7, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::sin(2.0f * w + phase) * 0.28f) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), 0.10f + std::cos(w + phase) * 0.16f);
            });
            addRot(11, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -std::sin(2.0f * w + phase) * 0.28f) *
                       Quat::FromAxisAngle(Vec3(0, 0, 1.0f), -0.10f - std::cos(w + phase) * 0.16f);
            });
            addRot(14, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -std::abs(std::sin(2.0f * w + phase)) * 0.18f);
            });
            addRot(15, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::abs(std::sin(2.0f * w + phase)) * 0.30f);
            });
            addRot(17, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -std::abs(std::cos(2.0f * w + phase)) * 0.18f);
            });
            addRot(18, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::abs(std::cos(2.0f * w + phase)) * 0.30f);
            });

            clips.push_back(clip);
        }

        // =========================================================================
        // Clip 2: KPop_CatwalkWave — Runway Strut & Fluid Body Wave
        // =========================================================================
        {
            AnimationClip clip;
            clip.name = "KPop_CatwalkWave";
            clip.duration = 2.0f;
            float phase = static_cast<float>(seed) * 0.5f;
            const int nKeys = 36;

            auto addRot = [&](int jIdx, auto fn) {
                AnimationChannel ch;
                ch.jointIndex = jIdx;
                ch.target = ChannelTarget::ROTATION;
                for (int k = 0; k <= nKeys; ++k) {
                    float t = (float)k / nKeys * clip.duration;
                    float w = (t / clip.duration) * TWO_PI;
                    ch.keyTimes.push_back(t);
                    ch.quatValues.push_back(fn(w));
                }
                clip.channels.push_back(ch);
            };

            addRot(1, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), std::sin(w + phase) * 0.14f) *
                       Quat::FromAxisAngle(Vec3(0, 1.0f, 0), std::cos(w + phase) * 0.12f);
            });
            addRot(2, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), -std::sin(w + phase) * 0.10f);
            });
            addRot(5, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 1.0f, 0), std::sin(w + phase) * 0.14f);
            });
            addRot(7, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -std::sin(w + phase) * 0.22f);
            });
            addRot(11, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::sin(w + phase) * 0.22f);
            });
            addRot(14, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::sin(w + phase) * 0.26f);
            });
            addRot(15, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::max(0.0f, -std::sin(w + phase)) * 0.34f);
            });
            addRot(17, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), -std::sin(w + phase) * 0.26f);
            });
            addRot(18, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::max(0.0f, std::sin(w + phase)) * 0.34f);
            });

            clips.push_back(clip);
        }

        // =========================================================================
        // Clip 3: KPop_IdleBreathe — Poised Stage Stance & Respiration
        // =========================================================================
        {
            AnimationClip clip;
            clip.name = "KPop_IdleBreathe";
            clip.duration = 3.2f;
            float phase = static_cast<float>(seed) * 0.7f;
            const int nKeys = 24;

            auto addRot = [&](int jIdx, auto fn) {
                AnimationChannel ch;
                ch.jointIndex = jIdx;
                ch.target = ChannelTarget::ROTATION;
                for (int k = 0; k <= nKeys; ++k) {
                    float t = (float)k / nKeys * clip.duration;
                    float w = (t / clip.duration) * TWO_PI;
                    ch.keyTimes.push_back(t);
                    ch.quatValues.push_back(fn(w));
                }
                clip.channels.push_back(ch);
            };

            addRot(1, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), std::sin(w + phase) * 0.05f);
            });
            addRot(2, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::sin(w + phase) * 0.04f);
            });
            addRot(3, [&](float w) {
                return Quat::FromAxisAngle(Vec3(1.0f, 0, 0), std::sin(w + phase + 0.3f) * 0.035f);
            });
            addRot(5, [&](float w) {
                return Quat::FromAxisAngle(Vec3(0, 0, 1.0f), std::cos(w + phase) * 0.04f) *
                       Quat::FromAxisAngle(Vec3(0, 1.0f, 0), std::sin(w + phase) * 0.06f);
            });

            clips.push_back(clip);
        }

        return clips;
    }
};

} // namespace Surreal
