#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - ADAPTIVE 3D FULL-BODY COSTUME & SKELETON RIGGER
 * ----------------------------------------------------------------------------
 * Builds a true 360-degree, articulated 3D full-body costume ("Ganzkoerper-
 * Kostuem") fitted directly onto a proportionally adapted real rigged 3D
 * skeleton (Mixamo / GLB skeleton with full bone hierarchy & animations):
 *
 *   1. Real 3D Skeleton Loader & Proportional Adapter:
 *      Loads the rigged 3D skeleton (skeleton_chicken_dance_1.glb /
 *      spooky_skeleton_dance_2.glb, fallback 20-joint humanoid rig) with all
 *      bone chains & animations, and proportionally adapts leg length, torso
 *      length, head size, shoulder span, hip width, and arm length to match
 *      the 2D subject while keeping uniform scaling ([+/-]) intact.
 *   2. Perimeter Color Decontamination & Clean Dual-Hemisphere Atlas:
 *      Removes dark/black cutout halos near silhouette edges, extracts clean
 *      interior garment/hair/skin pixels, replaces ONLY facial/throat skin
 *      on the back with matched hair/shoulder garment, and keeps backside
 *      garment brightness 1.0x identical to the front across seamless flanks.
 *   3. Articulated 3D Volumetric Costume Shell ("Ganzkoerper-Kostuem"):
 *      Generates closed, smooth-normal 3D anatomical tubes directly around
 *      the adapted skeleton bones (Head/Hair ovoid, Torso/Jacket/Skirt loft,
 *      separated Left/Right Sleeves+Hands, separated Left/Right Legs+Boots)
 *      with clean 360-degree UV mapping and per-limb linear blend skinning.
 *   4. Native + Retargeted Animation Clips:
 *      Preserves the loaded skeleton's native GLB animation plus world-delta
 *      retargeted real GLB dances and Idol/Idle choreographies.
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
#include <queue>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace Surreal {

struct RiggedFigureEntity {
    std::string name;
    Vec3 worldPosition;
    Vec3 localBoundsMin;
    Vec3 localBoundsMax;
    std::shared_ptr<SurrealMesh> mesh;
    std::shared_ptr<SkeletalMeshInstance> skeletal;
};

enum class HumanoidBoneRole {
    UNKNOWN = 0,
    HIPS,
    SPINE,
    SPINE1,
    SPINE2,
    NECK,
    HEAD,
    HEAD_TOP,
    L_SHOULDER,
    L_ARM,
    L_FOREARM,
    L_HAND,
    R_SHOULDER,
    R_ARM,
    R_FOREARM,
    R_HAND,
    L_UPLEG,
    L_LEG,
    L_FOOT,
    L_TOE,
    R_UPLEG,
    R_LEG,
    R_FOOT,
    R_TOE
};

class SurrealAutoRigger {
public:
    struct SilhouetteSlice {
        float uLeft = 0.25f;
        float uRight = 0.75f;
        float uCenter = 0.50f;
        float widthFrac = 0.35f;
        float torsoL = 0.30f;
        float torsoR = 0.70f;
        float legLL = 0.52f;
        float legLR = 0.70f;
        float legRL = 0.30f;
        float legRR = 0.48f;
        bool hasSplitLegs = false;
        ColorRGB avgColor = ColorRGB(0.25f, 0.25f, 0.28f);
        ColorRGB leftEdgeColor = ColorRGB(0.25f, 0.25f, 0.28f);
        ColorRGB rightEdgeColor = ColorRGB(0.25f, 0.25f, 0.28f);
    };

    struct SubjectProportions {
        float headEndFrac = 0.175f;
        float shoulderFrac = 0.21f;
        float waistFrac = 0.41f;
        float hipFrac = 0.50f;
        float crotchFrac = 0.52f;
        float skirtBottomFrac = -1.0f;
        bool hasSkirtOrCoat = false;
        bool hasSeparatedLegs = true;
        bool isLikelyCharacter = true;
        float fgCoverage = 0.0f;

        float bodyCenterU = 0.5f;
        float faceCenterU = 0.5f;
        float faceU0 = 0.30f;
        float faceU1 = 0.70f;
        float faceBottomFrac = 0.155f;
        float headHalfWRatio = 0.058f;     // relative to H
        float shoulderHalfWRatio = 0.135f; // relative to H
        float waistHalfWRatio = 0.088f;    // relative to H
        float hipHalfWRatio = 0.112f;      // relative to H
        float thighHalfWRatio = 0.048f;    // relative to H
        float armRadiusRatio = 0.036f;     // relative to H

        // Proportional bone multipliers applied to base 3D skeleton
        float legScale = 1.0f;
        float torsoScale = 1.0f;
        float headScale = 1.0f;
        float headWidthScale = 1.0f;
        float shoulderScale = 1.0f;
        float hipScale = 1.0f;
        float armScale = 1.0f;

        std::vector<SilhouetteSlice> slices; // 128 slices top (0) -> bottom (127)
    };

    static HumanoidBoneRole ClassifyJointRole(const std::string& rawName) {
        // Strip exporter numeric node suffixes (e.g. "Spine2_04", "LeftHand_011") BEFORE removing '_'
        std::string trimmed = rawName;
        size_t usPos = trimmed.rfind('_');
        if (usPos != std::string::npos && usPos + 1 < trimmed.size()) {
            bool allDigits = true;
            for (size_t k = usPos + 1; k < trimmed.size(); ++k) {
                if (!std::isdigit(static_cast<unsigned char>(trimmed[k]))) { allDigits = false; break; }
            }
            if (allDigits && trimmed.size() - (usPos + 1) >= 2) {
                trimmed = trimmed.substr(0, usPos);
            }
        }

        std::string n;
        n.reserve(trimmed.size());
        for (char ch : trimmed) {
            unsigned char c = static_cast<unsigned char>(ch);
            if (std::isalnum(c)) n.push_back(static_cast<char>(std::tolower(c)));
        }
        auto stripPrefix = [&](const std::string& p) {
            if (n.rfind(p, 0) == 0) n = n.substr(p.size());
        };
        stripPrefix("mixamorig");
        stripPrefix("ccbase");
        // Only strip >=2-digit trailing node indices starting with '0' (e.g. leftupleg024)
        // so single anatomical bone indices like "spine1" and "spine2" stay intact!
        if (n.size() >= 3 && std::isdigit(static_cast<unsigned char>(n.back()))) {
            size_t dStart = n.size();
            while (dStart > 0 && std::isdigit(static_cast<unsigned char>(n[dStart - 1]))) --dStart;
            size_t dLen = n.size() - dStart;
            if (dLen >= 2 && (n[dStart] == '0' || dLen >= 3)) {
                n.resize(dStart);
            }
        }
        // Reject finger/toe sub-twigs so thumb/index don't hijack LeftHand
        if (n.find("thumb") != std::string::npos || n.find("index") != std::string::npos ||
            n.find("middle") != std::string::npos || n.find("ring") != std::string::npos ||
            n.find("pinky") != std::string::npos || n.find("twist") != std::string::npos ||
            n.find("hojo") != std::string::npos || n.find("pole") != std::string::npos ||
            n.find("eye") != std::string::npos || n.find("jaw") != std::string::npos) {
            return HumanoidBoneRole::UNKNOWN;
        }

        if (n == "hips" || n == "pelvis" || n == "hip") return HumanoidBoneRole::HIPS;
        if (n == "spine") return HumanoidBoneRole::SPINE;
        if (n == "spine1" || n == "spinea") return HumanoidBoneRole::SPINE1;
        if (n == "spine2" || n == "chest" || n == "spine02" || n == "spine03") return HumanoidBoneRole::SPINE2;
        if (n == "neck") return HumanoidBoneRole::NECK;
        if (n == "head") return HumanoidBoneRole::HEAD;
        if (n.find("headtop") != std::string::npos || n == "headend") return HumanoidBoneRole::HEAD_TOP;

        bool isL = (n.rfind("left", 0) == 0 || n.rfind("l", 0) == 0);
        bool isR = (n.rfind("right", 0) == 0 || n.rfind("r", 0) == 0);
        if (n.find("shoulder") != std::string::npos) return isL ? HumanoidBoneRole::L_SHOULDER : (isR ? HumanoidBoneRole::R_SHOULDER : HumanoidBoneRole::UNKNOWN);
        if (n.find("forearm") != std::string::npos) return isL ? HumanoidBoneRole::L_FOREARM : (isR ? HumanoidBoneRole::R_FOREARM : HumanoidBoneRole::UNKNOWN);
        if (n.find("upperarm") != std::string::npos || n == "leftarm" || n == "rightarm" || n == "arml" || n == "armr") {
            return isL ? HumanoidBoneRole::L_ARM : (isR ? HumanoidBoneRole::R_ARM : HumanoidBoneRole::UNKNOWN);
        }
        if (n.find("hand") != std::string::npos) return isL ? HumanoidBoneRole::L_HAND : (isR ? HumanoidBoneRole::R_HAND : HumanoidBoneRole::UNKNOWN);
        if (n.find("upleg") != std::string::npos || n.find("thigh") != std::string::npos) {
            return isL ? HumanoidBoneRole::L_UPLEG : (isR ? HumanoidBoneRole::R_UPLEG : HumanoidBoneRole::UNKNOWN);
        }
        if (n.find("calf") != std::string::npos || n == "leftleg" || n == "rightleg" || n == "legl" || n == "legr") {
            return isL ? HumanoidBoneRole::L_LEG : (isR ? HumanoidBoneRole::R_LEG : HumanoidBoneRole::UNKNOWN);
        }
        if (n.find("toebase") != std::string::npos || n.find("toeend") != std::string::npos) {
            return isL ? HumanoidBoneRole::L_TOE : (isR ? HumanoidBoneRole::R_TOE : HumanoidBoneRole::UNKNOWN);
        }
        if (n.find("foot") != std::string::npos) return isL ? HumanoidBoneRole::L_FOOT : (isR ? HumanoidBoneRole::R_FOOT : HumanoidBoneRole::UNKNOWN);

        // Template 20-joint fallback aliases (Pelvis, Shoulder_L/R, Arm_L/R, Forearm_L/R, Hand_L/R, UpLeg_L/R, Leg_L/R, Foot_L/R)
        if (rawName == "Pelvis") return HumanoidBoneRole::HIPS;
        if (rawName == "Chest") return HumanoidBoneRole::SPINE2;
        if (rawName == "Shoulder_L") return HumanoidBoneRole::L_SHOULDER;
        if (rawName == "Shoulder_R") return HumanoidBoneRole::R_SHOULDER;
        if (rawName == "Arm_L") return HumanoidBoneRole::L_ARM;
        if (rawName == "Arm_R") return HumanoidBoneRole::R_ARM;
        if (rawName == "Forearm_L") return HumanoidBoneRole::L_FOREARM;
        if (rawName == "Forearm_R") return HumanoidBoneRole::R_FOREARM;
        if (rawName == "Hand_L") return HumanoidBoneRole::L_HAND;
        if (rawName == "Hand_R") return HumanoidBoneRole::R_HAND;
        if (rawName == "UpLeg_L") return HumanoidBoneRole::L_UPLEG;
        if (rawName == "UpLeg_R") return HumanoidBoneRole::R_UPLEG;
        if (rawName == "Leg_L") return HumanoidBoneRole::L_LEG;
        if (rawName == "Leg_R") return HumanoidBoneRole::R_LEG;
        if (rawName == "Foot_L") return HumanoidBoneRole::L_FOOT;
        if (rawName == "Foot_R") return HumanoidBoneRole::R_FOOT;

        return HumanoidBoneRole::UNKNOWN;
    }

    /**
     * Resolve joint node index & skin matrix slot index for a given role on any loaded rig.
     */
    static int FindJointByRole(const SkeletalMeshInstance& skel, HumanoidBoneRole role) {
        for (size_t i = 0; i < skel.joints.size(); ++i) {
            if (ClassifyJointRole(skel.joints[i].name) == role) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    static int FindSkinSlotForJoint(const SkeletalMeshInstance& skel, int jointNodeIdx) {
        if (jointNodeIdx < 0) return -1;
        if (!skel.skinJointNodeIndices.empty()) {
            for (size_t s = 0; s < skel.skinJointNodeIndices.size(); ++s) {
                if (skel.skinJointNodeIndices[s] == jointNodeIdx) return static_cast<int>(s);
            }
            return -1;
        }
        return (jointNodeIdx < static_cast<int>(skel.joints.size())) ? jointNodeIdx : -1;
    }

    /**
     * Quick classifier so Catalog [L] distinguishes character cutouts/portraits (full-body costume
     * over adapted 3D skeleton) from rectangular landscape/wall photo plates.
     */
    static bool ShouldBuildAsCostumedCharacter(const uint8_t* rgba, int w, int h) {
        if (!rgba || w < 8 || h < 8) return false;
        int total = 0, trans = 0;
        int step = std::max(1, std::min(w, h) / 64);
        for (int y = 0; y < h; y += step) {
            for (int x = 0; x < w; x += step) {
                ++total;
                if (rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] < 200) ++trans;
            }
        }
        if (total > 0 && trans * 100 > total * 3) return true; // Alpha cutout character

        // No alpha: check whether border pixels form a clean background vs upright subject
        std::vector<float> br, bg, bb;
        for (int x = 0; x < w; x += step) {
            const uint8_t* p0 = rgba + x * 4;
            const uint8_t* p1 = rgba + ((h - 1) * w + x) * 4;
            br.push_back(p0[0]); bg.push_back(p0[1]); bb.push_back(p0[2]);
            br.push_back(p1[0]); bg.push_back(p1[1]); bb.push_back(p1[2]);
        }
        for (int y = 0; y < h; y += step) {
            const uint8_t* p0 = rgba + (y * w) * 4;
            const uint8_t* p1 = rgba + (y * w + w - 1) * 4;
            br.push_back(p0[0]); bg.push_back(p0[1]); bb.push_back(p0[2]);
            br.push_back(p1[0]); bg.push_back(p1[1]); bb.push_back(p1[2]);
        }
        if (br.empty()) return false;
        auto med = [](std::vector<float>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        float mr = med(br), mg = med(bg), mb = med(bb);
        float var = 0.0f;
        for (size_t i = 0; i < br.size(); ++i) {
            float dr = br[i] - mr, dg = bg[i] - mg, db = bb[i] - mb;
            var += dr * dr + dg * dg + db * db;
        }
        float stdDev = std::sqrt(var / br.size());
        if (stdDev >= 42.0f) return false;

        // Require an actual upright subject inside the frame rather than a uniform plate or full-bleed wall
        int interiorSamples = 0, fgSamples = 0;
        int x0 = std::max(1, w / 10), x1 = std::min(w - 1, w - w / 10);
        int y0 = std::max(1, h / 12), y1 = std::min(h - 1, h - h / 12);
        int iStep = std::max(1, std::min(w, h) / 48);
        for (int y = y0; y < y1; y += iStep) {
            for (int x = x0; x < x1; x += iStep) {
                ++interiorSamples;
                const uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
                float dr = p[0] - mr, dg = p[1] - mg, db = p[2] - mb;
                if (dr * dr + dg * dg + db * db > 28.0f * 28.0f) ++fgSamples;
            }
        }
        if (interiorSamples == 0) return false;
        return (fgSamples * 100 >= interiorSamples * 6 && fgSamples * 100 <= interiorSamples * 88);
    }

    /**
     * Converts a single 2D character image into a Full-Body 3D Costume fitted over
     * a proportionally adapted real rigged 3D skeleton.
     */
    static RiggedFigureEntity BuildSingleRubberSuitCharacter(
        const std::string& imagePath,
        const std::string& charName = "Costume_Avatar",
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
        auto fig = BuildRubberSuitFromSubImage(
            rgba, imgW, imgH, 0.0f, 1.0f, charName,
            opt.heightMeters > 0.2f ? opt.heightMeters : 1.80f,
            animSeed, Vec3(0, 0, 0), true);
        stbi_image_free(rgba);
        return fig;
    }

    /**
     * Segments a group photo (e.g. MEOVV 5 members) and dresses each member in a
     * Full-Body 3D Costume onto their own proportionally adapted 3D skeleton.
     */
    static std::vector<RiggedFigureEntity> SegmentGroupAndRig(
        const std::string& imagePath,
        const Image3DOptions& opt = Image3DOptions(),
        float spacingOffset = 0.0f)
    {
        std::vector<RiggedFigureEntity> result;

        std::string resolvedGroupPath = surreal::paths::DefaultRigGroupImage(imagePath);
        int imgW = 0, imgH = 0, ch = 0;
        unsigned char* rgba = stbi_load(resolvedGroupPath.c_str(), &imgW, &imgH, &ch, 4);
        if (!rgba || imgW < 16 || imgH < 16) {
            if (rgba) stbi_image_free(rgba);
            std::string fb = surreal::paths::DefaultRigGroupImage();
            if (fb != resolvedGroupPath) {
                rgba = stbi_load(fb.c_str(), &imgW, &imgH, &ch, 4);
            } else {
                rgba = nullptr;
            }
        }

        if (!rgba || imgW < 16 || imgH < 16) {
            std::cerr << "[SurrealAutoRigger] Failed to load group image: " << imagePath << std::endl;
            if (rgba) stbi_image_free(rgba);
            return result;
        }

        struct MemberDef {
            std::string name;
            std::string filename;
            std::string fallbackFilename;
            float normMin;
            float normMax;
        };

        std::vector<MemberDef> members = {
            {"MEOVV_1_Gawon", "meovv_1_gawon.png", "meovv_1_ella.png",  0.070f, 0.275f},
            {"MEOVV_2_Anna",  "meovv_2_anna.png",  "meovv_2_gawon.png", 0.275f, 0.405f},
            {"MEOVV_3_Ella",  "meovv_3_ella.png",  "meovv_3_sooin.png", 0.405f, 0.545f},
            {"MEOVV_4_Narin", "meovv_4_narin.png", "meovv_4_anna.png",  0.545f, 0.665f},
            {"MEOVV_5_Sooin", "meovv_5_sooin.png", "meovv_5_narin.png", 0.665f, 0.835f}
        };

        float targetH = (opt.heightMeters > 0.5f) ? opt.heightMeters : 2.20f;
        const char* homeEnv = std::getenv("HOME");

        for (size_t figIdx = 0; figIdx < members.size(); ++figIdx) {
            const auto& def = members[figIdx];
            float zStagger = (figIdx % 2 == 1) ? 0.22f : -0.22f;
            float xPos = (static_cast<float>(figIdx) - 2.0f) * (1.12f + spacingOffset);
            float zPos = 4.2f + zStagger;
            Vec3 worldPos(xPos, 0.0f, zPos);

            RiggedFigureEntity fig;
            int mW = 0, mH = 0, mCh = 0;
            unsigned char* mRgba = nullptr;
            for (const std::string* fnPtr : {&def.filename, &def.fallbackFilename}) {
                std::string candidatePath = surreal::paths::ResolvePath(*fnPtr);
                mRgba = stbi_load(candidatePath.c_str(), &mW, &mH, &mCh, 4);
                if (mRgba && mW >= 16 && mH >= 16) break;
                if (mRgba) { stbi_image_free(mRgba); mRgba = nullptr; }
                if (homeEnv) {
                    std::string dlPath = std::string(homeEnv) + "/Downloads/" + *fnPtr;
                    mRgba = stbi_load(dlPath.c_str(), &mW, &mH, &mCh, 4);
                    if (mRgba && mW >= 16 && mH >= 16) break;
                    if (mRgba) { stbi_image_free(mRgba); mRgba = nullptr; }
                }
            }
            if (mRgba && mW >= 16 && mH >= 16) {
                fig = BuildRubberSuitFromSubImage(
                    mRgba, mW, mH, 0.0f, 1.0f, def.name, targetH, figIdx, worldPos, false);
                stbi_image_free(mRgba);
            } else {
                if (mRgba) stbi_image_free(mRgba);
                fig = BuildRubberSuitFromSubImage(
                    rgba, imgW, imgH, def.normMin, def.normMax, def.name, targetH, figIdx, worldPos, false);
            }

            if (fig.mesh && !fig.mesh->vertices.empty()) {
                result.push_back(fig);
            }
        }

        stbi_image_free(rgba);
        std::cout << "[SurrealAutoRigger] Dressed " << result.size()
                  << " 3D Full-Body Costumes on proportionally adapted rigged skeletons." << std::endl;
        return result;
    }

    /**
     * Core Full-Body 3D Costume + Proportional Skeleton Builder for a 2D sub-image region [uMin0, uMax0].
     */
    static RiggedFigureEntity BuildRubberSuitFromSubImage(
        const unsigned char* rgba, int imgW, int imgH,
        float uMin0, float uMax0,
        const std::string& name,
        float H,
        size_t figureSeed,
        const Vec3& worldPosition,
        bool preferNativeClipFirst = true)
    {
        RiggedFigureEntity fig;
        if (!rgba || imgW < 8 || imgH < 8) return fig;

        int xStart = std::max(0, std::min(imgW - 1, static_cast<int>(uMin0 * imgW)));
        int xEnd   = std::max(xStart + 1, std::min(imgW, static_cast<int>(uMax0 * imgW)));

        CleanCostumeAtlas atlas;
        SubjectProportions prop;
        if (!BuildCleanAtlasAndProportions(rgba, imgW, imgH, xStart, xEnd, atlas, prop)) {
            return fig;
        }

        // Load real rigged 3D skeleton (.glb Mixamo rig with fallback) & proportionally adapt bones to photo!
        auto inst = LoadAndAdaptBaseSkeleton(prop, H, figureSeed, preferNativeClipFirst);
        if (!inst) return fig;

        // Tailor closed 360-degree 3D articulated Full-Body Costume directly over the adapted skeleton bones
        auto costumeMesh = BuildCostumeMeshOverSkeleton(*inst, atlas, prop, H);
        if (!costumeMesh || costumeMesh->vertices.empty()) return fig;

        // Store bind vertices & bind deformed mesh
        inst->bindVertices.resize(costumeMesh->vertices.size());
        // Bind vertices + skin weights were generated cleanly inside BuildCostumeMeshOverSkeleton
        inst->bindVertices = std::move(atlas.generatedBindVerts);

        costumeMesh->CalculateBounds();
        costumeMesh->BuildClusters();
        inst->activeDeformedMesh->indices = std::move(costumeMesh->indices);
        inst->activeDeformedMesh->vertices = costumeMesh->vertices;
        inst->activeDeformedMesh->submeshes = std::move(costumeMesh->submeshes);
        inst->activeDeformedMesh->CalculateBounds();
        inst->activeDeformedMesh->BuildClusters();

        // Start playback cleanly & deform frame 0
        int startClip = 0;
        if (!preferNativeClipFirst && inst->clips.size() > 1) {
            for (size_t c = 0; c < inst->clips.size(); ++c) {
                if (inst->clips[c].name == "Idol_StageGroove") {
                    startClip = static_cast<int>(c);
                    break;
                }
            }
        }
        inst->PlayClip(startClip, true);
        inst->Update(0.0f, true);

        fig.name = name;
        fig.worldPosition = worldPosition;
        fig.localBoundsMin = inst->activeDeformedMesh->worldBounds.min;
        fig.localBoundsMax = inst->activeDeformedMesh->worldBounds.max;
        fig.mesh = inst->activeDeformedMesh;
        fig.skeletal = inst;
        return fig;
    }

    /**
     * Backward-compatible helper for wrapping an external static mesh onto a 20-joint humanoid rig.
     */
    static std::shared_ptr<SkeletalMeshInstance> BuildHumanoidTemplateSkeleton(
        const std::shared_ptr<SurrealMesh>& sourceMesh,
        float H, float W, size_t figureSeed)
    {
        auto inst = BuildSynthetic20JointSkeleton(H, W);
        if (!inst) return nullptr;

        if (sourceMesh && !sourceMesh->vertices.empty()) {
            auto smooth01 = [](float edge0, float edge1, float x) -> float {
                float t = std::clamp((x - edge0) / std::max(1e-5f, edge1 - edge0), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };
            inst->bindVertices.resize(sourceMesh->vertices.size());
            for (size_t v = 0; v < sourceMesh->vertices.size(); ++v) {
                const auto& vt = sourceMesh->vertices[v];
                SkinnedVertex3D sv;
                sv.position = vt.position;
                sv.normal = vt.normal;
                sv.u = vt.u;
                sv.v = vt.v;
                sv.color = vt.color;
                float yNorm = std::clamp(vt.position.y / std::max(0.1f, H), 0.0f, 1.0f);
                bool isLeft = (vt.position.x > 0.0f);
                if (yNorm < 0.45f) {
                    int bUp = isLeft ? 14 : 17;
                    int bLo = isLeft ? 15 : 18;
                    int bFt = isLeft ? 16 : 19;
                    if (yNorm < 0.14f) {
                        float t = smooth01(0.04f, 0.14f, yNorm);
                        sv.joints[0] = bFt; sv.weights[0] = 1.0f - t;
                        sv.joints[1] = bLo; sv.weights[1] = t;
                    } else {
                        float t = smooth01(0.18f, 0.42f, yNorm);
                        sv.joints[0] = bLo; sv.weights[0] = 1.0f - t;
                        sv.joints[1] = bUp; sv.weights[1] = t;
                    }
                } else {
                    if (yNorm > 0.84f) {
                        sv.joints[0] = 5; sv.weights[0] = 1.0f;
                    } else {
                        float t = smooth01(0.45f, 0.78f, yNorm);
                        sv.joints[0] = 1; sv.weights[0] = 1.0f - t;
                        sv.joints[1] = 3; sv.weights[1] = t;
                    }
                }
                inst->bindVertices[v] = sv;
            }
            inst->activeDeformedMesh->indices = sourceMesh->indices;
            inst->activeDeformedMesh->vertices = sourceMesh->vertices;
            inst->activeDeformedMesh->submeshes = sourceMesh->submeshes;
            inst->activeDeformedMesh->CalculateBounds();
            inst->activeDeformedMesh->BuildClusters();
        }
        inst->clips = GenerateProceduralClips(figureSeed, H);
        inst->PlayClip(0, true);
        inst->Update(0.0f, true);
        return inst;
    }

    /**
     * Generates 4 procedural K-Pop/Idol clips for the 20-joint synthetic rig (retained for API compatibility).
     */
    static std::vector<AnimationClip> GenerateProceduralClips(size_t seed, float H) {
        std::vector<AnimationClip> clips;
        auto proto = BuildSynthetic20JointSkeleton(H, H * 0.32f);
        if (!proto) return clips;
        proto->EvaluateRestPoseGlobals();
        clips.push_back(BuildIdolGrooveClip(*proto, seed));
        clips.push_back(BuildBunnyHopClip(*proto, seed));
        clips.push_back(BuildIdleBreatheClip(*proto, seed));
        return clips;
    }

private:
    struct CleanCostumeAtlas {
        int texW = 0;      // full dual-hemisphere atlas width = 2 * singleW
        int texH = 0;      // atlas height
        int singleW = 0;   // single front image width inside cropped working grid
        float skinU = 0.25f;
        float skinV = 0.14f;
        float shoeU = 0.25f;
        float shoeV = 0.94f;
        bool hasSampledSkin = false;
        std::vector<uint8_t> frontCleanRGBA; // singleW x texH (decontaminated)
        std::vector<uint8_t> atlasRGBA;      // texW x texH (left = front, right = matched back 1.0x)
        std::shared_ptr<std::vector<uint8_t>> sharedAtlasRGBA;
        std::vector<uint8_t> fg;             // singleW x texH foreground mask
        std::vector<float> dEff;             // chamfer distance to background
        std::vector<SkinnedVertex3D> generatedBindVerts;
    };

    static bool IsHumanSkinRGB(float r, float g, float b) {
        // Strict human skin filter: rejects crimson/red/pink/orange clothing where (r - g) is large
        if (r <= 82.0f || g <= 50.0f || b <= 35.0f) return false;
        if (r <= g || (r - g) < 8.0f || (r - g) > 76.0f) return false;
        if ((r - b) < 15.0f || g < b - 8.0f) return false;
        float sum = r + g + b + 1e-4f;
        float nr = r / sum;
        float ng = g / sum;
        return (nr > 0.365f && nr < 0.525f && ng > 0.275f && ng < 0.385f);
    }

    static bool BuildCleanAtlasAndProportions(
        const unsigned char* rgba, int imgW, int imgH,
        int xStart, int xEnd,
        CleanCostumeAtlas& outAtlas,
        SubjectProportions& outProp)
    {
        int subW = std::max(2, xEnd - xStart);

        // 1. Detect alpha vs uniform backdrop inside [xStart, xEnd]
        bool hasAlpha = false;
        int alphaSampleCount = 0, transCount = 0;
        for (int y = 0; y < imgH; y += 6) {
            for (int x = xStart; x < xEnd; x += 6) {
                ++alphaSampleCount;
                if (rgba[(static_cast<size_t>(y) * imgW + x) * 4 + 3] < 200) ++transCount;
            }
        }
        hasAlpha = (alphaSampleCount > 0 && transCount * 100 >= alphaSampleCount * 2);

        // Working grid scaled cleanly up to 896px so faces, eyes, hair and outfits remain razor sharp
        float gridScale = std::min(1.0f, 896.0f / static_cast<float>(std::max(subW, imgH)));
        int gw = std::max(16, static_cast<int>(std::lround(subW * gridScale)));
        int gh = std::max(24, static_cast<int>(std::lround(imgH * gridScale)));
        const int N = gw * gh;

        std::vector<float> R(N), G(N), B(N), A(N);
        for (int gy = 0; gy < gh; ++gy) {
            int sy = std::min(imgH - 1, static_cast<int>((static_cast<int64_t>(gy) * imgH) / gh));
            for (int gx = 0; gx < gw; ++gx) {
                int sx = xStart + std::min(subW - 1, static_cast<int>((static_cast<int64_t>(gx) * subW) / gw));
                const unsigned char* p = rgba + (static_cast<size_t>(sy) * imgW + sx) * 4;
                int i = gy * gw + gx;
                R[i] = p[0]; G[i] = p[1]; B[i] = p[2]; A[i] = p[3] / 255.0f;
            }
        }

        // Foreground mask with soft-garment hysteresis (preserves sheer & dark tights/skirts connected to core!)
        std::vector<uint8_t> fg(N, 0);
        if (hasAlpha) {
            std::queue<int> hq;
            for (int i = 0; i < N; ++i) {
                if (A[i] >= 0.36f) {
                    fg[i] = 1;
                    hq.push(i);
                }
            }
            while (!hq.empty()) {
                int cur = hq.front(); hq.pop();
                int x = cur % gw, y = cur / gw;
                int nbs[4] = { cur - 1, cur + 1, cur - gw, cur + gw };
                bool ok[4] = { x > 0, x + 1 < gw, y > 0, y + 1 < gh };
                for (int k = 0; k < 4; ++k) {
                    if (!ok[k]) continue;
                    int nb = nbs[k];
                    if (!fg[nb] && A[nb] >= 0.14f) {
                        fg[nb] = 1;
                        hq.push(nb);
                    }
                }
            }
        } else {
            // Edge-aware adaptive flood fill (handles flat backdrops AND radial studio vignettes like sample_mia.png)
            std::vector<float> br, bg, bb;
            auto pushB = [&](int i) { br.push_back(R[i]); bg.push_back(G[i]); bb.push_back(B[i]); };
            for (int x = 0; x < gw; ++x) { pushB(x); pushB((gh - 1) * gw + x); }
            for (int y = 1; y < gh - 1; ++y) { pushB(y * gw); pushB(y * gw + gw - 1); }
            auto med = [](std::vector<float>& v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
            float mr = med(br), mg = med(bg), mb = med(bb);
            float medSat = std::max({mr, mg, mb}) - std::min({mr, mg, mb});

            std::vector<float> rowRefR(gh, mr), rowRefG(gh, mg), rowRefB(gh, mb);
            for (int y = 0; y < gh; ++y) {
                int iL = y * gw, iR = y * gw + (gw - 1);
                rowRefR[y] = 0.5f * (R[iL] + R[iR]);
                rowRefG[y] = 0.5f * (G[iL] + G[iR]);
                rowRefB[y] = 0.5f * (B[iL] + B[iR]);
            }

            float var = 0.0f;
            for (size_t k = 0; k < br.size(); ++k) {
                float dr = br[k] - mr, dg = bg[k] - mg, db = bb[k] - mb;
                var += dr * dr + dg * dg + db * db;
            }
            float stdDev = std::sqrt(var / std::max<size_t>(1, br.size()));
            float seedThr = std::min(78.0f, std::max(26.0f, stdDev * 1.6f + 22.0f));
            float envThr  = std::min(105.0f, std::max(48.0f, stdDev * 2.2f + 42.0f));

            std::vector<uint8_t> isBg(N, 0);
            std::queue<int> q;
            auto trySeedBg = [&](int i) {
                if (isBg[i]) return;
                int y = i / gw;
                float dMed = std::sqrt((R[i] - mr) * (R[i] - mr) + (G[i] - mg) * (G[i] - mg) + (B[i] - mb) * (B[i] - mb));
                float dRow = std::sqrt((R[i] - rowRefR[y]) * (R[i] - rowRefR[y]) +
                                       (G[i] - rowRefG[y]) * (G[i] - rowRefG[y]) +
                                       (B[i] - rowRefB[y]) * (B[i] - rowRefB[y]));
                if (std::min(dMed, dRow) <= seedThr) { isBg[i] = 1; q.push(i); }
            };
            auto tryStepBg = [&](int cur, int nb) {
                if (isBg[nb]) return;
                float stepD = std::abs(R[nb] - R[cur]) + std::abs(G[nb] - G[cur]) + std::abs(B[nb] - B[cur]);
                int y = nb / gw;
                float dRow = std::sqrt((R[nb] - rowRefR[y]) * (R[nb] - rowRefR[y]) +
                                       (G[nb] - rowRefG[y]) * (G[nb] - rowRefG[y]) +
                                       (B[nb] - rowRefB[y]) * (B[nb] - rowRefB[y]));
                float dMed = std::sqrt((R[nb] - mr) * (R[nb] - mr) + (G[nb] - mg) * (G[nb] - mg) + (B[nb] - mb) * (B[nb] - mb));
                float nbSat = std::max({R[nb], G[nb], B[nb]}) - std::min({R[nb], G[nb], B[nb]});
                bool neutralGrad = (medSat < 22.0f && nbSat < 32.0f && stepD <= 26.0f && std::min(dRow, dMed) <= envThr * 1.15f);
                bool colorMatch  = (stepD <= 24.0f && std::min(dRow, dMed) <= seedThr * 1.15f);
                if (neutralGrad || colorMatch) { isBg[nb] = 1; q.push(nb); }
            };
            for (int x = 0; x < gw; ++x) { trySeedBg(x); trySeedBg((gh - 1) * gw + x); }
            for (int y = 0; y < gh; ++y) { trySeedBg(y * gw); trySeedBg(y * gw + gw - 1); }
            while (!q.empty()) {
                int cur = q.front(); q.pop();
                int x = cur % gw, y = cur / gw;
                if (x > 0) tryStepBg(cur, cur - 1);
                if (x + 1 < gw) tryStepBg(cur, cur + 1);
                if (y > 0) tryStepBg(cur, cur - gw);
                if (y + 1 < gh) tryStepBg(cur, cur + gw);
            }
            int fgCnt = 0;
            for (int i = 0; i < N; ++i) { fg[i] = isBg[i] ? 0 : 1; fgCnt += fg[i]; }
            if (fgCnt < N * 5 / 100 || fgCnt > N * 92 / 100) {
                std::fill(fg.begin(), fg.end(), 1);
            }
        }

        // Keep largest connected foreground component(s) to strip isolated edge specks before tight crop
        {
            std::vector<int> compId(N, -1);
            std::vector<int> compSize;
            std::queue<int> cq;
            for (int i = 0; i < N; ++i) {
                if (!fg[i] || compId[i] >= 0) continue;
                int cid = static_cast<int>(compSize.size());
                compId[i] = cid;
                cq.push(i);
                int sz = 0;
                while (!cq.empty()) {
                    int p = cq.front(); cq.pop();
                    ++sz;
                    int x = p % gw, y = p / gw;
                    if (x > 0 && fg[p - 1] && compId[p - 1] < 0) { compId[p - 1] = cid; cq.push(p - 1); }
                    if (x + 1 < gw && fg[p + 1] && compId[p + 1] < 0) { compId[p + 1] = cid; cq.push(p + 1); }
                    if (y > 0 && fg[p - gw] && compId[p - gw] < 0) { compId[p - gw] = cid; cq.push(p - gw); }
                    if (y + 1 < gh && fg[p + gw] && compId[p + gw] < 0) { compId[p + gw] = cid; cq.push(p + gw); }
                }
                compSize.push_back(sz);
            }
            if (!compSize.empty()) {
                int maxSz = *std::max_element(compSize.begin(), compSize.end());
                int keepMin = std::max(16, maxSz / 120);
                for (int i = 0; i < N; ++i) {
                    if (compId[i] >= 0 && compSize[compId[i]] < keepMin) fg[i] = 0;
                }
            }
        }

        // Crop strictly to tight foreground bounding box
        int minx = gw, maxx = -1, miny = gh, maxy = -1;
        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                if (fg[y * gw + x]) {
                    minx = std::min(minx, x); maxx = std::max(maxx, x);
                    miny = std::min(miny, y); maxy = std::max(maxy, y);
                }
            }
        }
        if (maxx <= minx + 2 || maxy <= miny + 4) return false;

        int cw = maxx - minx + 1;
        int ch = maxy - miny + 1;
        const int CN = cw * ch;

        std::vector<float> cR(CN), cG(CN), cB(CN), cA(CN);
        std::vector<uint8_t> cFg(CN, 0);
        int cFgCnt = 0;
        for (int y = 0; y < ch; ++y) {
            for (int x = 0; x < cw; ++x) {
                int si = (miny + y) * gw + (minx + x);
                int di = y * cw + x;
                cR[di] = R[si]; cG[di] = G[si]; cB[di] = B[si]; cA[di] = A[si];
                cFg[di] = fg[si];
                cFgCnt += cFg[di];
            }
        }
        if (cFgCnt < 16) return false;
        outProp.fgCoverage = static_cast<float>(cFgCnt) / static_cast<float>(CN);

        // 2. Interior chamfer distance for halo decontamination & Voronoi margin dilation
        std::vector<int> dist(CN, 0);
        const int INF = 1 << 28;
        for (int i = 0; i < CN; ++i) dist[i] = cFg[i] ? INF : 0;
        auto getD = [&](int x, int y) { return (x < 0 || y < 0 || x >= cw || y >= ch) ? 0 : dist[y * cw + x]; };
        for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) {
            int i = y * cw + x; if (!cFg[i]) continue;
            dist[i] = std::min({dist[i], getD(x-1,y)+3, getD(x,y-1)+3, getD(x-1,y-1)+4, getD(x+1,y-1)+4});
        }
        for (int y = ch - 1; y >= 0; --y) for (int x = cw - 1; x >= 0; --x) {
            int i = y * cw + x; if (!cFg[i]) continue;
            dist[i] = std::min({dist[i], getD(x+1,y)+3, getD(x,y+1)+3, getD(x+1,y+1)+4, getD(x-1,y+1)+4});
        }
        std::vector<float> dEff(CN, 0.0f);
        float maxDeff = 0.0f;
        for (int i = 0; i < CN; ++i) {
            dEff[i] = cFg[i] ? (dist[i] / 3.0f) : 0.0f;
            maxDeff = std::max(maxDeff, dEff[i]);
        }

        // Edge Halo Decontamination: replace dark matte perimeter fringes from clean interior (preserving sheer bright fabric)
        float cleanThresh = std::min(4.0f, std::max(1.5f, maxDeff * 0.45f));
        std::vector<float> cleanR = cR, cleanG = cG, cleanB = cB;
        std::vector<uint8_t> isValidInterior(CN, 0);
        for (int i = 0; i < CN; ++i) {
            bool pureAlpha = !hasAlpha || (cA[i] >= 0.85f);
            isValidInterior[i] = (cFg[i] && pureAlpha && dEff[i] >= cleanThresh) ? 1 : 0;
        }

        const int searchR = std::clamp(cw / 16, 8, 16);
        for (int y = 0; y < ch; ++y) {
            for (int x = 0; x < cw; ++x) {
                int i = y * cw + x;
                if (!cFg[i] || isValidInterior[i]) continue;
                bool isBrightSheer = (cR[i] + cG[i] + cB[i] > 500.0f && cA[i] >= 0.08f && cA[i] < 0.85f);
                if (isBrightSheer) continue;
                float bestSq = 1e9f;
                int bestJ = -1;
                for (int dy = -searchR; dy <= searchR; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= ch) continue;
                    for (int dx = -searchR; dx <= searchR; ++dx) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= cw) continue;
                        int j = yy * cw + xx;
                        if (!isValidInterior[j]) continue;
                        float d2 = static_cast<float>(dx * dx + dy * dy);
                        if (d2 < bestSq) { bestSq = d2; bestJ = j; }
                    }
                }
                if (bestJ >= 0) {
                    cleanR[i] = cR[bestJ];
                    cleanG[i] = cG[bestJ];
                    cleanB[i] = cB[bestJ];
                }
            }
        }

        // 3. Analyze 128 vertical slices & human proportions
        constexpr int NUM_SLICES = 128;
        outProp.slices.assign(NUM_SLICES, SilhouetteSlice());
        float figAspect = static_cast<float>(cw) / static_cast<float>(ch); // total bbox width / height

        struct Span { int l, r; };
        std::vector<std::vector<Span>> rowSpans(NUM_SLICES);
        std::vector<float> sliceWidth(NUM_SLICES, 0.0f);

        for (int s = 0; s < NUM_SLICES; ++s) {
            int y0 = (s * ch) / NUM_SLICES;
            int y1 = std::max(y0 + 1, ((s + 1) * ch) / NUM_SLICES);
            int yMid = std::min(ch - 1, (y0 + y1) / 2);

            // Detect contiguous horizontal foreground spans at yMid
            bool inSpan = false;
            int spL = 0;
            for (int x = 0; x < cw; ++x) {
                bool f = (cFg[yMid * cw + x] != 0);
                if (f && !inSpan) { inSpan = true; spL = x; }
                else if (!f && inSpan) { inSpan = false; if (x - spL >= 2) rowSpans[s].push_back({spL, x - 1}); }
            }
            if (inSpan && cw - spL >= 2) rowSpans[s].push_back({spL, cw - 1});

            int uL = cw, uR = -1;
            for (const auto& sp : rowSpans[s]) {
                uL = std::min(uL, sp.l);
                uR = std::max(uR, sp.r);
            }
            auto& sl = outProp.slices[s];
            if (uR > uL) {
                sl.uLeft = static_cast<float>(uL) / cw;
                sl.uRight = static_cast<float>(uR + 1) / cw;
                sl.uCenter = 0.5f * (sl.uLeft + sl.uRight);
                sl.widthFrac = sl.uRight - sl.uLeft;
            } else {
                sl.uLeft = 0.35f; sl.uRight = 0.65f; sl.uCenter = 0.5f; sl.widthFrac = 0.30f;
            }
            sliceWidth[s] = sl.widthFrac;

            // Sample slice colors strictly from clean interior pixels
            float sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int y = y0; y < y1 && y < ch; ++y) {
                for (int x = 0; x < cw; ++x) {
                    int idx = y * cw + x;
                    if (!cFg[idx]) continue;
                    sr += cleanR[idx]; sg += cleanG[idx]; sb += cleanB[idx];
                    cnt += 1.0f;
                }
            }
            if (cnt > 0.5f) {
                sl.avgColor = ColorRGB(sr / (cnt * 255.0f), sg / (cnt * 255.0f), sb / (cnt * 255.0f));
            }
        }

        // Median body horizontal center from upper torso (22%..55%)
        std::vector<float> centers;
        for (int s = NUM_SLICES * 22 / 100; s < NUM_SLICES * 55 / 100; ++s) {
            centers.push_back(outProp.slices[s].uCenter);
        }
        std::sort(centers.begin(), centers.end());
        outProp.bodyCenterU = centers.empty() ? 0.5f : centers[centers.size() / 2];

        // Detect Head -> Neck transition & tight facial skin box within cranial band (2%..20%)
        auto isPortraitSkin = [&](float r, float g, float b) -> bool {
            if (IsHumanSkinRGB(r, g, b)) return true;
            return (r >= 68.0f && g >= 52.0f && b >= 45.0f && r >= g - 2.0f && r > b + 5.0f && (r - b) <= 95.0f && !(r > g * 1.45f && g < 110.0f));
        };
        float maxHeadW = 0.0f;
        int headPeakIdx = NUM_SLICES * 8 / 100;
        std::vector<float> cranialCenters, cranialWidths, cranialLefts, cranialRights;
        for (int s = NUM_SLICES * 3 / 100; s <= NUM_SLICES * 13 / 100; ++s) {
            if (sliceWidth[s] > maxHeadW) { maxHeadW = sliceWidth[s]; headPeakIdx = s; }
            cranialCenters.push_back(outProp.slices[s].uCenter);
            cranialWidths.push_back(outProp.slices[s].widthFrac);
            cranialLefts.push_back(outProp.slices[s].uLeft);
            cranialRights.push_back(outProp.slices[s].uRight);
        }
        float cranialCenter = outProp.bodyCenterU;
        float cranialMedianW = std::max(0.15f, maxHeadW * 0.85f);
        float cranLeft = std::clamp(cranialCenter - 0.5f * cranialMedianW, 0.02f, 0.88f);
        float cranRight = std::clamp(cranialCenter + 0.5f * cranialMedianW, cranLeft + 0.06f, 0.98f);
        if (!cranialCenters.empty()) {
            std::sort(cranialCenters.begin(), cranialCenters.end());
            std::sort(cranialWidths.begin(), cranialWidths.end());
            std::sort(cranialLefts.begin(), cranialLefts.end());
            std::sort(cranialRights.begin(), cranialRights.end());
            cranialCenter = cranialCenters[cranialCenters.size() / 2];
            cranialMedianW = cranialWidths[cranialWidths.size() * 3 / 5];
            cranLeft = cranialLefts[cranialLefts.size() / 4];
            cranRight = cranialRights[cranialRights.size() * 3 / 4];
        }

        // Detect earliest shoulder widening (>= +30% wider than cranial crown) to cap chin/neck before torso
        int firstShoulderSlice = NUM_SLICES * 20 / 100;
        for (int s = NUM_SLICES * 13 / 100; s <= NUM_SLICES * 22 / 100; ++s) {
            if (sliceWidth[s] > cranialMedianW * 1.30f) {
                firstShoulderSlice = s;
                break;
            }
        }

        int lastFaceSkinSlice = -1;
        int faceSlicesSeen = 0;
        std::vector<float> faceSliceCenterUs, faceSliceWidths;
        int faceScanEnd = std::min(NUM_SLICES * 18 / 100, firstShoulderSlice);
        int halfScan = std::clamp(static_cast<int>(0.42f * cranialMedianW * cw), 6, cw / 3);
        int cranXc = static_cast<int>(cranialCenter * cw);
        for (int s = NUM_SLICES * 3 / 100; s <= faceScanEnd; ++s) {
            int y0 = (s * ch) / NUM_SLICES, y1 = std::max(y0 + 1, ((s + 1) * ch) / NUM_SLICES);
            int skinHit = 0, fgHit = 0;
            int minSx = cw, maxSx = -1;
            int xc = static_cast<int>(0.65f * cranXc + 0.35f * outProp.slices[s].uCenter * cw);
            for (int y = y0; y < y1 && y < ch; ++y) {
                for (int x = std::max(0, xc - halfScan); x < std::min(cw, xc + halfScan); ++x) {
                    int idx = y * cw + x;
                    if (!cFg[idx]) continue;
                    ++fgHit;
                    if (isPortraitSkin(cleanR[idx], cleanG[idx], cleanB[idx])) {
                        ++skinHit;
                        minSx = std::min(minSx, x);
                        maxSx = std::max(maxSx, x);
                    }
                }
            }
            if (fgHit > 2 && skinHit * 100 >= fgHit * 16) {
                ++faceSlicesSeen;
                lastFaceSkinSlice = s;
                if (maxSx > minSx + 2) {
                    faceSliceCenterUs.push_back((0.5f * (minSx + maxSx) + 0.5f) / cw);
                    faceSliceWidths.push_back(static_cast<float>(maxSx - minSx + 1) / cw);
                }
            }
        }

        int minNeckSlice = std::max(headPeakIdx + 2, NUM_SLICES * 12 / 100);
        int maxNeckSearch = std::max(minNeckSlice + 1, std::min(NUM_SLICES * 22 / 100, firstShoulderSlice + 1));
        int neckIdx = std::min(maxNeckSearch, minNeckSlice);
        float minNeckW = 10.0f;
        for (int s = minNeckSlice; s <= maxNeckSearch; ++s) {
            if (sliceWidth[s] < minNeckW) { minNeckW = sliceWidth[s]; neckIdx = s; }
        }
        if (faceSlicesSeen >= 2 && lastFaceSkinSlice >= NUM_SLICES * 9 / 100) {
            int skinFloor = std::min(NUM_SLICES * 19 / 100, lastFaceSkinSlice + 1);
            int skinCeil = std::min(maxNeckSearch + 1, lastFaceSkinSlice + 3);
            neckIdx = std::clamp(neckIdx, skinFloor, std::max(skinFloor, skinCeil));
        }
        outProp.headEndFrac = std::clamp(static_cast<float>(neckIdx) / NUM_SLICES, 0.145f, 0.21f);
        if (faceSlicesSeen >= 2 && lastFaceSkinSlice >= NUM_SLICES * 9 / 100) {
            outProp.faceBottomFrac = std::clamp(static_cast<float>(lastFaceSkinSlice + 0.5f) / NUM_SLICES,
                                                0.125f, std::min(0.168f, outProp.headEndFrac - 0.008f));
        } else {
            outProp.faceBottomFrac = std::clamp(outProp.headEndFrac - 0.014f, 0.125f, 0.168f);
        }
        outProp.shoulderFrac = std::clamp(outProp.headEndFrac + 0.025f, 0.170f, 0.28f);
        outProp.waistFrac = std::clamp(outProp.shoulderFrac + 0.18f, 0.34f, 0.52f);

        bool hasClearFaceSkin = (faceSliceWidths.size() >= 3);
        float medianSkinWFrac = maxHeadW * 0.55f;
        outProp.faceCenterU = cranialCenter;
        if (hasClearFaceSkin) {
            std::sort(faceSliceCenterUs.begin(), faceSliceCenterUs.end());
            std::sort(faceSliceWidths.begin(), faceSliceWidths.end());
            float skinCenter = faceSliceCenterUs[faceSliceCenterUs.size() / 2];
            outProp.faceCenterU = std::clamp(0.65f * skinCenter + 0.35f * cranialCenter,
                                             std::max(0.15f, outProp.bodyCenterU - 0.22f),
                                             std::min(0.85f, outProp.bodyCenterU + 0.22f));
            medianSkinWFrac = faceSliceWidths[faceSliceWidths.size() * 3 / 5];
        }
        // Constrain faceHalfU strictly within [cranLeft..cranRight] so ZERO empty gutter smears onto cheeks/ears!
        float leftAvail = std::max(0.08f, outProp.faceCenterU - cranLeft + 0.004f);
        float rightAvail = std::max(0.08f, cranRight - outProp.faceCenterU + 0.004f);
        float targetHalfU = hasClearFaceSkin
            ? std::max(0.5f * medianSkinWFrac * 1.45f, 0.5f * cranialMedianW * 0.96f)
            : (0.5f * std::max(maxHeadW * 0.92f, cranialMedianW * 0.96f));
        float faceHalfU = std::clamp(std::min(targetHalfU, std::min(leftAvail, rightAvail)), 0.14f, 0.32f);
        outProp.faceU0 = std::clamp(outProp.faceCenterU - faceHalfU, 0.02f, 0.88f);
        outProp.faceU1 = std::clamp(outProp.faceCenterU + faceHalfU, outProp.faceU0 + 0.08f, 0.98f);

        // Extract Torso spans cleanly (pick central span containing bodyCenterU if arms are separated!)
        for (int s = 0; s < NUM_SLICES; ++s) {
            auto& sl = outProp.slices[s];
            const auto& spans = rowSpans[s];
            if (spans.empty()) continue;

            // Largest or most central span for torso/upper body
            float centerX = outProp.bodyCenterU * cw;
            int bestIdx = 0;
            float bestDist = 1e9f;
            for (size_t k = 0; k < spans.size(); ++k) {
                float sc = 0.5f * (spans[k].l + spans[k].r);
                float d = std::abs(sc - centerX);
                if (centerX >= spans[k].l && centerX <= spans[k].r) d *= 0.1f;
                if (d < bestDist) { bestDist = d; bestIdx = static_cast<int>(k); }
            }
            const Span& central = spans[bestIdx];
            float insetPx = std::min(4.0f, 0.08f * (central.r - central.l + 1));
            sl.torsoL = std::clamp((central.l + insetPx) / cw, 0.0f, 0.98f);
            sl.torsoR = std::clamp((central.r + 1.0f - insetPx) / cw, sl.torsoL + 0.02f, 1.0f);

            // Detect split Left/Right legs below waist
            float yFrac = static_cast<float>(s) / NUM_SLICES;
            if (yFrac > 0.44f && spans.size() >= 2) {
                // Find widest left-of-center & right-of-center spans
                int lBest = -1, rBest = -1;
                int lBestW = 0, rBestW = 0;
                for (size_t k = 0; k < spans.size(); ++k) {
                    int wSp = spans[k].r - spans[k].l + 1;
                    float sc = 0.5f * (spans[k].l + spans[k].r);
                    if (sc < centerX && wSp > lBestW) { lBestW = wSp; lBest = static_cast<int>(k); }
                    if (sc >= centerX && wSp > rBestW) { rBestW = wSp; rBest = static_cast<int>(k); }
                }
                if (lBest >= 0 && rBest >= 0 && lBest != rBest &&
                    lBestW >= std::max(2, cw / 18) && rBestW >= std::max(2, cw / 18)) {
                    sl.hasSplitLegs = true;
                    float insL = std::min(3.0f, 0.10f * lBestW);
                    float insR = std::min(3.0f, 0.10f * rBestW);
                    // Viewer's left = Character Right (-X), Viewer's right = Character Left (+X)
                    sl.legRL = std::clamp((spans[lBest].l + insL) / cw, 0.0f, 0.96f);
                    sl.legRR = std::clamp((spans[lBest].r + 1.0f - insL) / cw, sl.legRL + 0.02f, 1.0f);
                    sl.legLL = std::clamp((spans[rBest].l + insR) / cw, 0.0f, 0.96f);
                    sl.legLR = std::clamp((spans[rBest].r + 1.0f - insR) / cw, sl.legLL + 0.02f, 1.0f);
                }
            }
            if (!sl.hasSplitLegs) {
                float uMid = 0.5f * (sl.uLeft + sl.uRight);
                float halfW = std::max(0.04f, 0.5f * (sl.uRight - sl.uLeft));
                float ins = std::min(0.03f, halfW * 0.12f);
                sl.legRL = std::clamp(sl.uLeft + ins, 0.0f, uMid - 0.01f);
                sl.legRR = std::clamp(uMid - ins * 0.5f, sl.legRL + 0.02f, 1.0f);
                sl.legLL = std::clamp(uMid + ins * 0.5f, 0.0f, 0.98f);
                sl.legLR = std::clamp(sl.uRight - ins, sl.legLL + 0.02f, 1.0f);
            }
        }

        // Detect crotch / leg split height & possible skirt/long coat
        int splitFirst = -1;
        int consecSplit = 0;
        for (int s = NUM_SLICES * 44 / 100; s < NUM_SLICES * 86 / 100; ++s) {
            if (outProp.slices[s].hasSplitLegs) {
                if (splitFirst < 0) splitFirst = s;
                if (++consecSplit >= 3) break;
            } else {
                splitFirst = -1;
                consecSplit = 0;
            }
        }
        if (splitFirst >= 0) {
            outProp.crotchFrac = std::clamp(static_cast<float>(splitFirst) / NUM_SLICES, 0.44f, 0.72f);
            outProp.hasSeparatedLegs = true;
            if (outProp.crotchFrac > 0.56f) {
                outProp.hasSkirtOrCoat = true;
                outProp.skirtBottomFrac = outProp.crotchFrac;
            }
        } else {
            outProp.crotchFrac = 0.52f;
            outProp.hasSeparatedLegs = false;
        }
        outProp.hipFrac = std::clamp(outProp.crotchFrac - 0.03f, outProp.waistFrac + 0.04f, 0.68f);

        // Compute normalized physical body widths relative to full character height (1.0 = heightMeters)
        auto torsoWidthAt = [&](float yF) -> float {
            int idx = std::clamp(static_cast<int>(yF * (NUM_SLICES - 1)), 0, NUM_SLICES - 1);
            const auto& sl = outProp.slices[idx];
            return (sl.torsoR - sl.torsoL) * figAspect;
        };
        float rawHeadHalfWRatio = 0.5f * maxHeadW * figAspect;
        bool isWideMascot = (rawHeadHalfWRatio > 0.125f);
        float humanCranialHalfW = std::max(0.5f * medianSkinWFrac * figAspect * 1.24f,
                                           0.5f * cranialMedianW * figAspect * 0.82f);
        if (isWideMascot) {
            outProp.headHalfWRatio = std::clamp(rawHeadHalfWRatio * 0.72f, 0.062f, 0.115f);
        } else {
            outProp.headHalfWRatio = std::clamp(
                hasClearFaceSkin ? humanCranialHalfW : (rawHeadHalfWRatio * 0.68f),
                0.056f, 0.068f);
        }
        float chestHalfW = 0.0f;
        for (float yf = outProp.headEndFrac + 0.035f; yf <= std::min(outProp.waistFrac, outProp.headEndFrac + 0.145f); yf += 0.012f) {
            chestHalfW = std::max(chestHalfW, 0.5f * torsoWidthAt(yf));
        }
        outProp.shoulderHalfWRatio = std::clamp(chestHalfW, 0.105f, 0.25f);
        outProp.waistHalfWRatio = std::clamp(0.5f * torsoWidthAt(outProp.waistFrac), 0.072f, 0.22f);
        outProp.hipHalfWRatio = std::clamp(0.5f * torsoWidthAt(outProp.hipFrac), 0.088f, 0.24f);

        float legWAcc = 0.0f; int legWCnt = 0;
        for (int s = static_cast<int>(outProp.crotchFrac * NUM_SLICES); s < NUM_SLICES * 90 / 100; ++s) {
            const auto& sl = outProp.slices[s];
            float lw = 0.25f * ((sl.legRR - sl.legRL) + (sl.legLR - sl.legLL)) * figAspect;
            legWAcc += lw;
            ++legWCnt;
        }
        outProp.thighHalfWRatio = std::clamp(legWCnt > 0 ? (legWAcc / legWCnt) : 0.048f, 0.038f, 0.075f);
        outProp.armRadiusRatio = std::clamp(outProp.shoulderHalfWRatio * 0.27f, 0.030f, 0.065f);

        // Compute Proportional Skeleton Bone Scales vs Standard Humanoid Proportions!
        float imgLegLenFrac = std::clamp(1.0f - outProp.crotchFrac, 0.30f, 0.60f);
        float imgTorsoLenFrac = std::clamp(outProp.crotchFrac - outProp.headEndFrac, 0.18f, 0.48f);
        float imgHeadLenFrac = std::clamp(outProp.headEndFrac, 0.12f, 0.40f);

        outProp.legScale = std::clamp(imgLegLenFrac / 0.50f, 0.72f, 1.30f);
        outProp.torsoScale = std::clamp(imgTorsoLenFrac / 0.33f, 0.76f, 1.32f);
        if (isWideMascot) {
            outProp.headWidthScale = std::clamp(rawHeadHalfWRatio / 0.075f, 0.88f, 1.80f);
            outProp.headScale = std::max(std::clamp(imgHeadLenFrac / 0.17f, 0.90f, 1.65f), 1.0f);
        } else {
            outProp.headWidthScale = std::clamp(rawHeadHalfWRatio / 0.075f, 0.86f, 1.16f);
            outProp.headScale = std::max(std::clamp(imgHeadLenFrac / 0.17f, 0.90f, 1.32f), outProp.headWidthScale * 0.96f);
        }
        outProp.shoulderScale = std::clamp(outProp.shoulderHalfWRatio / 0.130f, 0.78f, 1.60f);
        outProp.hipScale = std::clamp(outProp.hipHalfWRatio / 0.105f, 0.78f, 1.55f);
        outProp.armScale = std::clamp(0.55f * outProp.torsoScale + 0.45f * outProp.shoulderScale, 0.80f, 1.30f);

        // 4. Synthesize Clean Matched Backside (1.0x brightness, clean garment preservation!)
        // Estimate crown/temple hair colors strictly from upper head cap
        int headEndY = std::clamp(static_cast<int>(outProp.headEndFrac * ch), 4, ch - 1);
        int crownEndY = std::max(2, static_cast<int>(headEndY * 0.42f));
        std::vector<float> colHairR(cw, 0.0f), colHairG(cw, 0.0f), colHairB(cw, 0.0f);
        std::vector<uint8_t> hasColHair(cw, 0);
        float avgHairR = 0.0f, avgHairG = 0.0f, avgHairB = 0.0f, avgHairCnt = 0.0f;

        for (int x = 0; x < cw; ++x) {
            float hr = 0, hg = 0, hb = 0, hc = 0;
            for (int y = 0; y <= crownEndY && y < ch; ++y) {
                int idx = y * cw + x;
                if (!cFg[idx]) continue;
                float r = cleanR[idx], g = cleanG[idx], b = cleanB[idx];
                if (!IsHumanSkinRGB(r, g, b)) {
                    hr += r; hg += g; hb += b; hc += 1.0f;
                }
            }
            if (hc > 0.5f) {
                colHairR[x] = hr / hc; colHairG[x] = hg / hc; colHairB[x] = hb / hc;
                hasColHair[x] = 1;
                avgHairR += colHairR[x]; avgHairG += colHairG[x]; avgHairB += colHairB[x];
                avgHairCnt += 1.0f;
            }
        }
        if (avgHairCnt > 0.5f) {
            avgHairR /= avgHairCnt; avgHairG /= avgHairCnt; avgHairB /= avgHairCnt;
        } else {
            // Character without distinct hair cap (e.g. smooth mascot/hat): sample upper head interior
            float hr = 0, hg = 0, hb = 0, hc = 0;
            for (int y = 1; y < std::max(3, headEndY / 2); ++y) {
                for (int x = cw / 4; x < cw * 3 / 4; ++x) {
                    int idx = y * cw + x;
                    if (cFg[idx]) { hr += cleanR[idx]; hg += cleanG[idx]; hb += cleanB[idx]; hc += 1.0f; }
                }
            }
            if (hc > 0.5f) { avgHairR = hr / hc; avgHairG = hg / hc; avgHairB = hb / hc; }
            else { avgHairR = 45.0f; avgHairG = 40.0f; avgHairB = 42.0f; }
        }
        for (int x = 0; x < cw; ++x) {
            if (!hasColHair[x]) { colHairR[x] = avgHairR; colHairG[x] = avgHairG; colHairB[x] = avgHairB; }
        }

        // Check if head front actually contains human/anime face skin (if so, cover entire back of skull with hair!)
        auto isBackCleanupSkin = [&](float r, float g, float b) -> bool {
            if (IsHumanSkinRGB(r, g, b)) return true;
            return (r >= 40.0f && g >= 30.0f && b >= 26.0f && r >= g - 2.0f && r > b + 4.0f && (r - b) <= 95.0f && !(r > g * 1.45f && g < 110.0f));
        };
        int faceSkinPx = 0, faceTotalPx = 0;
        for (int y = std::max(1, headEndY / 5); y < headEndY; ++y) {
            for (int x = cw / 5; x < cw * 4 / 5; ++x) {
                int idx = y * cw + x;
                if (!cFg[idx]) continue;
                ++faceTotalPx;
                if (isBackCleanupSkin(cleanR[idx], cleanG[idx], cleanB[idx])) ++faceSkinPx;
            }
        }
        bool hasFaceInHead = (faceTotalPx > 12 && faceSkinPx * 100 >= faceTotalPx * 12);

        std::vector<float> backR = cleanR, backG = cleanG, backB = cleanB;
        int hipCleanBotY = std::min(ch - 1, static_cast<int>(outProp.hipFrac * ch));

        for (int y = 0; y < ch; ++y) {
            float yFrac = static_cast<float>(y) / static_cast<float>(ch);
            int slIdx = std::clamp(static_cast<int>(yFrac * (NUM_SLICES - 1)), 0, NUM_SLICES - 1);
            int spanLpx = static_cast<int>(outProp.slices[slIdx].torsoL * cw);
            int spanRpx = static_cast<int>(outProp.slices[slIdx].torsoR * cw);
            int spanW = std::max(4, spanRpx - spanLpx);
            int spanMid = (spanLpx + spanRpx) / 2;

            for (int x = 0; x < cw; ++x) {
                int idx = y * cw + x;
                if (!cFg[idx]) continue;
                float r = cleanR[idx], g = cleanG[idx], b = cleanB[idx];

                if (y <= headEndY) {
                    // Replace entire face/bangs/eyes oval on back of skull with matched crown hair!
                    bool inFaceEllip = hasFaceInHead && (y >= headEndY * 0.14f) &&
                                       (std::abs(x - spanMid) < std::max(3, spanW * 45 / 100));
                    if (inFaceEllip || isBackCleanupSkin(r, g, b)) {
                        float strand = 0.96f + 0.04f * std::sin(x * 1.9f + y * 0.25f);
                        backR[idx] = std::clamp(colHairR[x] * strand, 0.0f, 255.0f);
                        backG[idx] = std::clamp(colHairG[x] * strand, 0.0f, 255.0f);
                        backB[idx] = std::clamp(colHairB[x] * strand, 0.0f, 255.0f);
                    }
                } else if (y <= hipCleanBotY && isBackCleanupSkin(r, g, b) &&
                           std::abs(x - spanMid) <= spanW * 42 / 100) {
                    // Replace front throat / neckline / chest-hand skin on back of torso via nearest 2D non-skin neighbor!
                    int bestSq = 1 << 28;
                    int bestJ = -1;
                    int maxR = std::min(28, std::max(8, cw / 3));
                    for (int dy = -maxR; dy <= maxR; ++dy) {
                        int yy = y + dy;
                        if (yy <= headEndY || yy >= ch) continue;
                        for (int dx = -maxR; dx <= maxR; ++dx) {
                            int d2 = dx * dx + dy * dy;
                            if (d2 == 0 || d2 >= bestSq) continue;
                            int xx = x + dx;
                            if (xx < spanLpx || xx > spanRpx) continue;
                            int j = yy * cw + xx;
                            if (cFg[j] && !isBackCleanupSkin(cleanR[j], cleanG[j], cleanB[j])) {
                                bestSq = d2;
                                bestJ = j;
                            }
                        }
                    }
                    if (bestJ >= 0) {
                        backR[idx] = cleanR[bestJ];
                        backG[idx] = cleanG[bestJ];
                        backB[idx] = cleanB[bestJ];
                    }
                }
            }
        }

        // Voronoi Edge Dilation into background pixels so bilinear UV sampling near seams NEVER hits black!
        auto dilateIntoBg = [&](std::vector<float>& chR, std::vector<float>& chG, std::vector<float>& chB) {
            std::vector<uint8_t> filled = cFg;
            std::queue<int> dq;
            for (int y = 0; y < ch; ++y) {
                for (int x = 0; x < cw; ++x) {
                    int i = y * cw + x;
                    if (!filled[i]) continue;
                    if ((x > 0 && !filled[i - 1]) || (x + 1 < cw && !filled[i + 1]) ||
                        (y > 0 && !filled[i - cw]) || (y + 1 < ch && !filled[i + cw])) {
                        dq.push(i);
                    }
                }
            }
            while (!dq.empty()) {
                int cur = dq.front(); dq.pop();
                int x = cur % cw, y = cur / cw;
                int nbs[4] = { cur - 1, cur + 1, cur - cw, cur + cw };
                bool ok[4] = { x > 0, x + 1 < cw, y > 0, y + 1 < ch };
                for (int k = 0; k < 4; ++k) {
                    if (!ok[k]) continue;
                    int nb = nbs[k];
                    if (!filled[nb]) {
                        filled[nb] = 1;
                        chR[nb] = chR[cur];
                        chG[nb] = chG[cur];
                        chB[nb] = chB[cur];
                        dq.push(nb);
                    }
                }
            }
        };
        dilateIntoBg(cleanR, cleanG, cleanB);
        dilateIntoBg(backR, backG, backB);

        // Sample cleanest facial/hand skin pixel & lower shoe/boot pixel inside front atlas
        {
            struct SkinCand { int x, y; float lum; };
            std::vector<SkinCand> skinCands;
            for (int y = ch * 4 / 100; y < ch * 55 / 100; ++y) {
                for (int x = 0; x < cw; ++x) {
                    int idx = y * cw + x;
                    if (!isValidInterior[idx]) continue;
                    float r = cleanR[idx], g = cleanG[idx], b = cleanB[idx];
                    if (IsHumanSkinRGB(r, g, b)) {
                        skinCands.push_back({x, y, 0.299f * r + 0.587f * g + 0.114f * b});
                    }
                }
            }
            if (!skinCands.empty()) {
                std::sort(skinCands.begin(), skinCands.end(), [](const SkinCand& a, const SkinCand& b) { return a.lum < b.lum; });
                const SkinCand& sc = skinCands[skinCands.size() * 3 / 5];
                outAtlas.hasSampledSkin = true;
                outAtlas.skinU = 0.002f + ((sc.x + 0.5f) / static_cast<float>(cw)) * 0.494f;
                outAtlas.skinV = (sc.y + 0.5f) / static_cast<float>(ch);
            } else {
                outAtlas.hasSampledSkin = false;
                outAtlas.skinU = 0.002f + outProp.faceCenterU * 0.494f;
                outAtlas.skinV = std::clamp(outProp.headEndFrac * 0.65f, 0.05f, 0.25f);
            }

            std::vector<SkinCand> shoeCands;
            for (int y = ch * 85 / 100; y < ch * 97 / 100; ++y) {
                for (int x = 0; x < cw; ++x) {
                    int idx = y * cw + x;
                    if (!isValidInterior[idx]) continue;
                    float lum = 0.299f * cleanR[idx] + 0.587f * cleanG[idx] + 0.114f * cleanB[idx];
                    shoeCands.push_back({x, y, lum});
                }
            }
            if (shoeCands.size() < 4) {
                // Bottom faded out (e.g. mist/sheer skirt): pick darkest clean solid garment pixel for crisp boots
                for (int y = ch * 32 / 100; y < ch * 85 / 100; ++y) {
                    for (int x = 0; x < cw; ++x) {
                        int idx = y * cw + x;
                        if (!isValidInterior[idx]) continue;
                        float lum = 0.299f * cleanR[idx] + 0.587f * cleanG[idx] + 0.114f * cleanB[idx];
                        if (lum < 140.0f) shoeCands.push_back({x, y, lum});
                    }
                }
            }
            if (!shoeCands.empty()) {
                std::vector<SkinCand> shoeLitCands;
                shoeLitCands.reserve(shoeCands.size());
                for (const auto& sc : shoeCands) {
                    if (sc.lum >= 35.0f) shoeLitCands.push_back(sc);
                }
                auto& pool = !shoeLitCands.empty() ? shoeLitCands : shoeCands;
                std::sort(pool.begin(), pool.end(), [](const SkinCand& a, const SkinCand& b) { return a.lum < b.lum; });
                const SkinCand& sc = pool[pool.size() / 2];
                outAtlas.shoeU = 0.002f + ((sc.x + 0.5f) / static_cast<float>(cw)) * 0.494f;
                outAtlas.shoeV = (sc.y + 0.5f) / static_cast<float>(ch);
            } else {
                outAtlas.shoeU = 0.002f + outProp.bodyCenterU * 0.494f;
                outAtlas.shoeV = 0.90f;
            }
        }

        // Pack into Dual-Hemisphere GPU/CPU RGBA Atlas (texW = 2 * cw, texH = ch)
        outAtlas.singleW = cw;
        outAtlas.texW = cw * 2;
        outAtlas.texH = ch;
        outAtlas.fg = std::move(cFg);
        outAtlas.dEff = std::move(dEff);
        outAtlas.frontCleanRGBA.resize(static_cast<size_t>(CN) * 4);
        outAtlas.atlasRGBA.resize(static_cast<size_t>(outAtlas.texW) * outAtlas.texH * 4);

        for (int y = 0; y < ch; ++y) {
            for (int x = 0; x < cw; ++x) {
                int i = y * cw + x;
                auto toByte = [](float v) -> uint8_t {
                    return static_cast<uint8_t>(std::clamp(static_cast<int>(v + 0.5f), 0, 255));
                };
                uint8_t fr = toByte(cleanR[i]), fgCol = toByte(cleanG[i]), fbCol = toByte(cleanB[i]);
                uint8_t br = toByte(backR[i]),  bgCol = toByte(backG[i]),  bbCol = toByte(backB[i]);

                outAtlas.frontCleanRGBA[static_cast<size_t>(i) * 4 + 0] = fr;
                outAtlas.frontCleanRGBA[static_cast<size_t>(i) * 4 + 1] = fgCol;
                outAtlas.frontCleanRGBA[static_cast<size_t>(i) * 4 + 2] = fbCol;
                outAtlas.frontCleanRGBA[static_cast<size_t>(i) * 4 + 3] = 255;

                size_t fIdx = (static_cast<size_t>(y) * outAtlas.texW + x) * 4;
                outAtlas.atlasRGBA[fIdx + 0] = fr;
                outAtlas.atlasRGBA[fIdx + 1] = fgCol;
                outAtlas.atlasRGBA[fIdx + 2] = fbCol;
                outAtlas.atlasRGBA[fIdx + 3] = 255;

                size_t bIdx = (static_cast<size_t>(y) * outAtlas.texW + (cw + x)) * 4;
                outAtlas.atlasRGBA[bIdx + 0] = br;
                outAtlas.atlasRGBA[bIdx + 1] = bgCol;
                outAtlas.atlasRGBA[bIdx + 2] = bbCol;
                outAtlas.atlasRGBA[bIdx + 3] = 255;
            }
        }
        outAtlas.sharedAtlasRGBA = std::make_shared<std::vector<uint8_t>>(outAtlas.atlasRGBA);
        return true;
    }

    static ColorRGB SampleAtlasColor(const CleanCostumeAtlas& atlas, float u, float v) {
        if (atlas.texW <= 0 || atlas.texH <= 0 || atlas.atlasRGBA.empty()) return ColorRGB(1, 1, 1);
        float fx = std::clamp(u, 0.0f, 0.9999f) * (atlas.texW - 1);
        float fy = std::clamp(v, 0.0f, 0.9999f) * (atlas.texH - 1);
        int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
        int x1 = std::min(atlas.texW - 1, x0 + 1);
        int y1 = std::min(atlas.texH - 1, y0 + 1);
        float tx = fx - x0, ty = fy - y0;
        auto px = [&](int x, int y) -> ColorRGB {
            size_t idx = (static_cast<size_t>(y) * atlas.texW + x) * 4;
            return ColorRGB(atlas.atlasRGBA[idx] / 255.0f, atlas.atlasRGBA[idx + 1] / 255.0f, atlas.atlasRGBA[idx + 2] / 255.0f);
        };
        ColorRGB c00 = px(x0, y0), c10 = px(x1, y0), c01 = px(x0, y1), c11 = px(x1, y1);
        ColorRGB top = c00 * (1.0f - tx) + c10 * tx;
        ColorRGB bot = c01 * (1.0f - tx) + c11 * tx;
        return top * (1.0f - ty) + bot * ty;
    }

    // ---- Rotation Helpers for World-Delta Skeleton Retargeting ----
    static Quat BasisToQuat(const Mat4& m) {
        Vec3 c0(m.m[0][0], m.m[1][0], m.m[2][0]);
        Vec3 c1(m.m[0][1], m.m[1][1], m.m[2][1]);
        Vec3 c2(m.m[0][2], m.m[1][2], m.m[2][2]);
        float l0 = c0.Length(), l1 = c1.Length(), l2 = c2.Length();
        if (l0 > 1e-6f) c0 = c0 / l0; else c0 = Vec3(1, 0, 0);
        if (l1 > 1e-6f) c1 = c1 / l1; else c1 = Vec3(0, 1, 0);
        if (l2 > 1e-6f) c2 = c2 / l2; else c2 = Vec3(0, 0, 1);

        float trace = c0.x + c1.y + c2.z;
        Quat q;
        if (trace > 0.0f) {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (c1.z - c2.y) / s;
            q.y = (c2.x - c0.z) / s;
            q.z = (c0.y - c1.x) / s;
        } else if (c0.x > c1.y && c0.x > c2.z) {
            float s = std::sqrt(1.0f + c0.x - c1.y - c2.z) * 2.0f;
            q.w = (c1.z - c2.y) / s;
            q.x = 0.25f * s;
            q.y = (c1.x + c0.y) / s;
            q.z = (c2.x + c0.z) / s;
        } else if (c1.y > c2.z) {
            float s = std::sqrt(1.0f + c1.y - c0.x - c2.z) * 2.0f;
            q.w = (c2.x - c0.z) / s;
            q.x = (c1.x + c0.y) / s;
            q.y = 0.25f * s;
            q.z = (c2.y + c1.z) / s;
        } else {
            float s = std::sqrt(1.0f + c2.z - c0.x - c1.y) * 2.0f;
            q.w = (c0.y - c1.x) / s;
            q.x = (c2.x + c0.z) / s;
            q.y = (c2.y + c1.z) / s;
            q.z = 0.25f * s;
        }
        return NormalizeQuat(q);
    }

    static Quat NormalizeQuat(const Quat& q) {
        float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (lenSq < 1e-12f) return Quat(0.0f, 0.0f, 0.0f, 1.0f);
        float inv = 1.0f / std::sqrt(lenSq);
        return Quat(q.x * inv, q.y * inv, q.z * inv, q.w * inv);
    }

    static Quat SlerpQuat(const Quat& a, const Quat& b, float t) {
        float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        Quat bAdj = (dot < 0.0f) ? Quat(-b.x, -b.y, -b.z, -b.w) : b;
        if (dot < 0.0f) dot = -dot;
        if (dot > 0.9995f) {
            return NormalizeQuat(Quat(
                a.x + (bAdj.x - a.x) * t,
                a.y + (bAdj.y - a.y) * t,
                a.z + (bAdj.z - a.z) * t,
                a.w + (bAdj.w - a.w) * t));
        }
        float theta0 = std::acos(std::clamp(dot, -1.0f, 1.0f));
        float theta = theta0 * t;
        float sinTheta = std::sin(theta);
        float sinTheta0 = std::sin(theta0);
        float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        float s1 = sinTheta / sinTheta0;
        return NormalizeQuat(Quat(
            s0 * a.x + s1 * bAdj.x,
            s0 * a.y + s1 * bAdj.y,
            s0 * a.z + s1 * bAdj.z,
            s0 * a.w + s1 * bAdj.w));
    }

    static Quat Conjugate(const Quat& q) {
        return Quat(-q.x, -q.y, -q.z, q.w);
    }

    static Quat SampleClipRotationAt(const AnimationClip& clip, int jointIdx, float t, const Quat& fallback) {
        for (const auto& ch : clip.channels) {
            if (ch.jointIndex != jointIdx || ch.target != ChannelTarget::ROTATION || ch.quatValues.empty() || ch.keyTimes.empty()) {
                continue;
            }
            if (ch.keyTimes.size() == 1 || ch.quatValues.size() == 1) return ch.quatValues[0];
            auto itUp = std::upper_bound(ch.keyTimes.begin(), ch.keyTimes.end(), t);
            size_t k = 0;
            if (itUp == ch.keyTimes.begin()) k = 0;
            else if (itUp == ch.keyTimes.end()) k = ch.keyTimes.size() - 2;
            else k = static_cast<size_t>(std::distance(ch.keyTimes.begin(), itUp)) - 1;
            float t0 = ch.keyTimes[k], t1 = ch.keyTimes[k + 1];
            float f = (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
            return SlerpQuat(ch.quatValues[k], ch.quatValues[std::min(k + 1, ch.quatValues.size() - 1)], f);
        }
        return fallback;
    }

    static Quat WorldDeltaToLocalRot(const SkeletalMeshInstance& skel, int jointIdx, const Quat& worldDelta) {
        if (jointIdx < 0 || jointIdx >= static_cast<int>(skel.joints.size())) return Quat(0, 0, 0, 1);
        const auto& j = skel.joints[jointIdx];
        Quat qParentRest = (j.parentIndex >= 0) ? BasisToQuat(skel.joints[j.parentIndex].globalMatrix) : Quat(0, 0, 0, 1);
        Quat qJointRest = BasisToQuat(j.globalMatrix);
        return NormalizeQuat(Conjugate(qParentRest) * worldDelta * qJointRest);
    }

    static AnimationClip RetargetExternalClip(
        const SkeletalMeshInstance& srcSkel, const AnimationClip& srcClip,
        const SkeletalMeshInstance& dstSkel, const std::string& clipName)
    {
        AnimationClip out;
        out.name = clipName;
        out.duration = srcClip.duration;
        if (srcClip.duration <= 0.01f) return out;

        // Pre-index source role -> joint
        std::unordered_map<int, int> srcRoleToJoint;
        for (size_t i = 0; i < srcSkel.joints.size(); ++i) {
            HumanoidBoneRole r = ClassifyJointRole(srcSkel.joints[i].name);
            if (r != HumanoidBoneRole::UNKNOWN && srcRoleToJoint.find(static_cast<int>(r)) == srcRoleToJoint.end()) {
                srcRoleToJoint[static_cast<int>(r)] = static_cast<int>(i);
            }
        }

        const int numSamples = std::max(16, std::min(72, static_cast<int>(srcClip.duration * 24.0f)));
        for (size_t di = 0; di < dstSkel.joints.size(); ++di) {
            HumanoidBoneRole role = ClassifyJointRole(dstSkel.joints[di].name);
            if (role == HumanoidBoneRole::UNKNOWN) continue;
            auto itSrc = srcRoleToJoint.find(static_cast<int>(role));
            if (itSrc == srcRoleToJoint.end()) continue;
            int si = itSrc->second;
            int sp = srcSkel.joints[si].parentIndex;
            Quat qSrcParentRest = (sp >= 0) ? BasisToQuat(srcSkel.joints[sp].globalMatrix) : Quat(0, 0, 0, 1);
            Quat qSrcJointRest = BasisToQuat(srcSkel.joints[si].globalMatrix);

            AnimationChannel ch;
            ch.jointIndex = static_cast<int>(di);
            ch.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= numSamples; ++k) {
                float t = (static_cast<float>(k) / numSamples) * srcClip.duration;
                Quat qSrcLocalAnim = SampleClipRotationAt(srcClip, si, t, srcSkel.joints[si].baseRotation);
                Quat qSrcAnimGlobal = qSrcParentRest * qSrcLocalAnim;
                Quat worldDelta = NormalizeQuat(qSrcAnimGlobal * Conjugate(qSrcJointRest));
                ch.keyTimes.push_back(t);
                ch.quatValues.push_back(WorldDeltaToLocalRot(dstSkel, static_cast<int>(di), worldDelta));
            }
            out.channels.push_back(std::move(ch));
        }
        return out;
    }

    static AnimationClip BuildIdolGrooveClip(const SkeletalMeshInstance& skel, size_t seed) {
        AnimationClip clip;
        clip.name = "Idol_StageGroove";
        clip.duration = 2.6667f;
        const float TWO_PI = 6.28318530718f;
        const float phase = static_cast<float>(seed) * 0.45f;
        const int nKeys = 32;

        auto addWorldDelta = [&](HumanoidBoneRole role, auto fn) {
            int jIdx = FindJointByRole(skel, role);
            if (jIdx < 0) return;
            AnimationChannel ch;
            ch.jointIndex = jIdx;
            ch.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (static_cast<float>(k) / nKeys) * clip.duration;
                float w = (t / clip.duration) * TWO_PI;
                ch.keyTimes.push_back(t);
                ch.quatValues.push_back(WorldDeltaToLocalRot(skel, jIdx, fn(w)));
            }
            clip.channels.push_back(std::move(ch));
        };

        // Stylistic A-Pose standing Idol dance: natural lowered shoulders, flowing elbows, hip groove!
        addWorldDelta(HumanoidBoneRole::HIPS, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 1, 0), std::sin(w + phase) * 0.14f) *
                   Quat::FromAxisAngle(Vec3(0, 0, 1), std::sin(w * 2.0f + phase) * 0.06f);
        });
        addWorldDelta(HumanoidBoneRole::SPINE1, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.04f + std::sin(w * 2.0f + phase) * 0.05f) *
                   Quat::FromAxisAngle(Vec3(0, 1, 0), -std::sin(w + phase) * 0.10f);
        });
        addWorldDelta(HumanoidBoneRole::SPINE2, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 0, 1), std::cos(w + phase) * 0.07f);
        });
        addWorldDelta(HumanoidBoneRole::HEAD, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 1, 0), std::sin(w + phase) * 0.10f) *
                   Quat::FromAxisAngle(Vec3(0, 0, 1), -std::cos(w * 2.0f + phase) * 0.05f);
        });
        // Lower arms cleanly from T-Pose into natural A-Pose groove (-1.12 rad around +Z for +X LeftArm, +1.12 for -X RightArm)
        addWorldDelta(HumanoidBoneRole::L_ARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.16f + std::sin(w + phase) * 0.18f) *
                   Quat::FromAxisAngle(Vec3(0, 0, 1), -1.10f + std::cos(w * 2.0f + phase) * 0.10f);
        });
        addWorldDelta(HumanoidBoneRole::L_FOREARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.45f - std::sin(w * 2.0f + phase) * 0.25f);
        });
        addWorldDelta(HumanoidBoneRole::R_ARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.16f - std::sin(w + phase) * 0.18f) *
                   Quat::FromAxisAngle(Vec3(0, 0, 1), 1.10f - std::cos(w * 2.0f + phase) * 0.10f);
        });
        addWorldDelta(HumanoidBoneRole::R_FOREARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.45f + std::sin(w * 2.0f + phase) * 0.25f);
        });
        // Rhythmic alternating knee groove
        addWorldDelta(HumanoidBoneRole::L_UPLEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.10f * std::max(0.0f, std::sin(w + phase)));
        });
        addWorldDelta(HumanoidBoneRole::L_LEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.20f * std::max(0.0f, std::sin(w + phase)));
        });
        addWorldDelta(HumanoidBoneRole::R_UPLEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.10f * std::max(0.0f, -std::sin(w + phase)));
        });
        addWorldDelta(HumanoidBoneRole::R_LEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.20f * std::max(0.0f, -std::sin(w + phase)));
        });
        return clip;
    }

    static AnimationClip BuildBunnyHopClip(const SkeletalMeshInstance& skel, size_t seed) {
        AnimationClip clip;
        clip.name = "KPop_BunnyHop";
        clip.duration = 2.0f;
        const float TWO_PI = 6.28318530718f;
        const float phase = static_cast<float>(seed) * 0.4f;
        const int nKeys = 24;

        auto addWorldDelta = [&](HumanoidBoneRole role, auto fn) {
            int jIdx = FindJointByRole(skel, role);
            if (jIdx < 0) return;
            AnimationChannel ch;
            ch.jointIndex = jIdx;
            ch.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (static_cast<float>(k) / nKeys) * clip.duration;
                float w = (t / clip.duration) * TWO_PI;
                ch.keyTimes.push_back(t);
                ch.quatValues.push_back(WorldDeltaToLocalRot(skel, jIdx, fn(w)));
            }
            clip.channels.push_back(std::move(ch));
        };

        addWorldDelta(HumanoidBoneRole::HIPS, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 1, 0), std::sin(w + phase) * 0.16f);
        });
        addWorldDelta(HumanoidBoneRole::L_ARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 0, 1), -0.65f + std::sin(w * 2.0f + phase) * 0.25f) *
                   Quat::FromAxisAngle(Vec3(1, 0, 0), 0.30f);
        });
        addWorldDelta(HumanoidBoneRole::L_FOREARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.85f + std::cos(w * 2.0f + phase) * 0.35f);
        });
        addWorldDelta(HumanoidBoneRole::R_ARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 0, 1), 0.65f - std::sin(w * 2.0f + phase) * 0.25f) *
                   Quat::FromAxisAngle(Vec3(1, 0, 0), 0.30f);
        });
        addWorldDelta(HumanoidBoneRole::R_FOREARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.85f - std::cos(w * 2.0f + phase) * 0.35f);
        });
        addWorldDelta(HumanoidBoneRole::L_UPLEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.22f * std::max(0.0f, std::sin(w + phase)));
        });
        addWorldDelta(HumanoidBoneRole::L_LEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.42f * std::max(0.0f, std::sin(w + phase)));
        });
        addWorldDelta(HumanoidBoneRole::R_UPLEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.22f * std::max(0.0f, -std::sin(w + phase)));
        });
        addWorldDelta(HumanoidBoneRole::R_LEG, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), 0.42f * std::max(0.0f, -std::sin(w + phase)));
        });
        return clip;
    }

    static AnimationClip BuildIdleBreatheClip(const SkeletalMeshInstance& skel, size_t seed) {
        AnimationClip clip;
        clip.name = "Idle_ShowcaseStand";
        clip.duration = 3.2f;
        const float TWO_PI = 6.28318530718f;
        const float phase = static_cast<float>(seed) * 0.5f;
        const int nKeys = 16;

        auto addWorldDelta = [&](HumanoidBoneRole role, auto fn) {
            int jIdx = FindJointByRole(skel, role);
            if (jIdx < 0) return;
            AnimationChannel ch;
            ch.jointIndex = jIdx;
            ch.target = ChannelTarget::ROTATION;
            for (int k = 0; k <= nKeys; ++k) {
                float t = (static_cast<float>(k) / nKeys) * clip.duration;
                float w = (t / clip.duration) * TWO_PI;
                ch.keyTimes.push_back(t);
                ch.quatValues.push_back(WorldDeltaToLocalRot(skel, jIdx, fn(w)));
            }
            clip.channels.push_back(std::move(ch));
        };

        // Natural calm A-pose showcase stand (gentle breathing + easy 360° visual inspection!)
        addWorldDelta(HumanoidBoneRole::SPINE2, [&](float w) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), std::sin(w + phase) * 0.025f);
        });
        addWorldDelta(HumanoidBoneRole::HEAD, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 1, 0), std::sin(w + phase) * 0.05f);
        });
        addWorldDelta(HumanoidBoneRole::L_ARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 0, 1), -1.16f + std::sin(w + phase) * 0.02f);
        });
        addWorldDelta(HumanoidBoneRole::L_FOREARM, [&](float /*w*/) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.18f);
        });
        addWorldDelta(HumanoidBoneRole::R_ARM, [&](float w) {
            return Quat::FromAxisAngle(Vec3(0, 0, 1), 1.16f - std::sin(w + phase) * 0.02f);
        });
        addWorldDelta(HumanoidBoneRole::R_FOREARM, [&](float /*w*/) {
            return Quat::FromAxisAngle(Vec3(1, 0, 0), -0.18f);
        });
        return clip;
    }

    static std::shared_ptr<SkeletalMeshInstance> BuildSynthetic20JointSkeleton(float H, float W) {
        auto inst = std::make_shared<SkeletalMeshInstance>();
        struct JointDef { std::string name; int parent; Vec3 worldPos; };
        float shSpan = std::max(0.24f, W);

        std::vector<JointDef> defs = {
            {"Root",        -1, Vec3(0, 0, 0)},
            {"Pelvis",       0, Vec3(0, 0.50f * H, 0)},
            {"Spine",        1, Vec3(0, 0.62f * H, 0)},
            {"Chest",        2, Vec3(0, 0.74f * H, 0)},
            {"Neck",         3, Vec3(0, 0.83f * H, 0)},
            {"Head",         4, Vec3(0, 0.92f * H, 0)},
            {"Shoulder_L",   3, Vec3(shSpan * 0.40f, 0.75f * H, 0)},
            {"Arm_L",        6, Vec3(shSpan * 0.45f, 0.75f * H, 0)},
            {"Forearm_L",    7, Vec3(shSpan * 0.45f + 0.24f * H, 0.75f * H, 0)},
            {"Hand_L",       8, Vec3(shSpan * 0.45f + 0.46f * H, 0.75f * H, 0)},
            {"Shoulder_R",   3, Vec3(-shSpan * 0.40f, 0.75f * H, 0)},
            {"Arm_R",       10, Vec3(-shSpan * 0.45f, 0.75f * H, 0)},
            {"Forearm_R",   11, Vec3(-shSpan * 0.45f - 0.24f * H, 0.75f * H, 0)},
            {"Hand_R",      12, Vec3(-shSpan * 0.45f - 0.46f * H, 0.75f * H, 0)},
            {"UpLeg_L",      1, Vec3(shSpan * 0.24f, 0.47f * H, 0)},
            {"Leg_L",       14, Vec3(shSpan * 0.24f, 0.25f * H, 0)},
            {"Foot_L",      15, Vec3(shSpan * 0.24f, 0.03f * H, 0)},
            {"UpLeg_R",      1, Vec3(-shSpan * 0.24f, 0.47f * H, 0)},
            {"Leg_R",       17, Vec3(-shSpan * 0.24f, 0.25f * H, 0)},
            {"Foot_R",      18, Vec3(-shSpan * 0.24f, 0.03f * H, 0)}
        };

        inst->joints.resize(defs.size());
        for (size_t i = 0; i < defs.size(); ++i) {
            auto& j = inst->joints[i];
            j.name = defs[i].name;
            j.parentIndex = defs[i].parent;
            Vec3 pPos = (defs[i].parent >= 0) ? defs[defs[i].parent].worldPos : Vec3(0, 0, 0);
            j.baseTranslation = defs[i].worldPos - pPos;
            j.localTranslation = j.baseTranslation;
            j.baseRotation = Quat(0, 0, 0, 1);
            j.localRotation = j.baseRotation;
            j.baseScale = Vec3(1, 1, 1);
            j.localScale = j.baseScale;
            j.inverseBindMatrix = Mat4::Translation(Vec3(-defs[i].worldPos.x, -defs[i].worldPos.y, -defs[i].worldPos.z));
        }
        return inst;
    }

    struct CachedBaseRigs {
        bool initialized = false;
        std::shared_ptr<SkeletalMeshInstance> primarySkeleton;
        std::vector<AnimationClip> extraRetargetedClips;
    };

    static const CachedBaseRigs& GetCachedBaseRigs() {
        static CachedBaseRigs cache;
        if (cache.initialized) return cache;
        cache.initialized = true;

        std::vector<std::string> skelCandidates;
        if (const char* envSkel = std::getenv("SURREAL_BASE_SKELETON")) {
            if (envSkel[0] != '\0') skelCandidates.push_back(envSkel);
        }
        skelCandidates.push_back("assets/models/skeleton_chicken_dance_1.glb");
        skelCandidates.push_back("assets/models/spooky_skeleton_dance_2.glb");

        for (const auto& c : skelCandidates) {
            std::string p = surreal::paths::ResolveAsset(c);
            std::error_code ec;
            if (!p.empty() && std::filesystem::exists(p, ec)) {
                auto loaded = std::make_shared<SkeletalMeshInstance>();
                if (SurrealSkeletalLoader::LoadSkeletalModel(p, *loaded) && !loaded->joints.empty()) {
                    // Strip raw bone geometry so only our costume covers it, keeping complete bone rig & animations!
                    loaded->bindVertices.clear();
                    loaded->activeDeformedMesh = std::make_shared<SurrealMesh>();
                    loaded->EvaluateRestPoseGlobals();
                    cache.primarySkeleton = loaded;
                    break;
                }
            }
        }

        if (cache.primarySkeleton) {
            // Load optional additional Mixamo dance animations from disk & retarget cleanly onto primarySkeleton
            struct ExtraClipSrc { const char* relPath; const char* title; };
            const ExtraClipSrc extras[] = {
                {"assets/models/spooky_skeleton_dance_2.glb", "Spooky_SkeleDance"},
                {"assets/models/wednesday_gangnam_style.glb", "Gangnam_Style"}
            };
            for (const auto& ex : extras) {
                std::string p = surreal::paths::ResolveAsset(ex.relPath);
                std::error_code ec;
                if (p.empty() || !std::filesystem::exists(p, ec)) continue;
                SkeletalMeshInstance src;
                if (SurrealSkeletalLoader::LoadSkeletalModel(p, src) && !src.clips.empty()) {
                    src.EvaluateRestPoseGlobals();
                    auto ret = RetargetExternalClip(src, src.clips[0], *cache.primarySkeleton, ex.title);
                    if (!ret.channels.empty()) {
                        cache.extraRetargetedClips.push_back(std::move(ret));
                    }
                }
            }
        }
        return cache;
    }

    static std::shared_ptr<SkeletalMeshInstance> LoadAndAdaptBaseSkeleton(
        const SubjectProportions& prop,
        float targetH,
        size_t animSeed,
        bool preferNativeClipFirst)
    {
        const auto& cache = GetCachedBaseRigs();
        auto inst = std::make_shared<SkeletalMeshInstance>();

        if (cache.primarySkeleton) {
            inst->joints = cache.primarySkeleton->joints;
            inst->skinJointNodeIndices = cache.primarySkeleton->skinJointNodeIndices;
            inst->skinJointIBMs = cache.primarySkeleton->skinJointIBMs;
            inst->clips = cache.primarySkeleton->clips;
            if (!inst->clips.empty() && inst->clips[0].name == "mixamo.com") {
                inst->clips[0].name = "Skeleton_ChickenDance";
            }
        } else {
            auto syn = BuildSynthetic20JointSkeleton(targetH, targetH * 0.28f);
            inst->joints = std::move(syn->joints);
        }

        // 1. Apply Proportional Skeleton Adaptation onto actual bones (preserved at ALL animation frames!)
        for (auto& j : inst->joints) {
            HumanoidBoneRole role = ClassifyJointRole(j.name);
            switch (role) {
                case HumanoidBoneRole::HIPS:
                    j.boneLengthScale = Vec3(prop.hipScale, prop.legScale, prop.hipScale);
                    break;
                case HumanoidBoneRole::SPINE:
                    j.boneLengthScale = Vec3(0.5f * prop.hipScale + 0.5f * prop.torsoScale, prop.torsoScale, prop.hipScale);
                    break;
                case HumanoidBoneRole::SPINE1: {
                    float midW = 0.5f * prop.hipScale + 0.5f * prop.shoulderScale;
                    j.boneLengthScale = Vec3(midW, prop.torsoScale, midW);
                    break;
                }
                case HumanoidBoneRole::SPINE2:
                    j.boneLengthScale = Vec3(prop.shoulderScale, prop.torsoScale, prop.shoulderScale);
                    break;
                case HumanoidBoneRole::NECK:
                    j.boneLengthScale = Vec3(prop.torsoScale, prop.torsoScale, prop.torsoScale);
                    break;
                case HumanoidBoneRole::HEAD:
                case HumanoidBoneRole::HEAD_TOP:
                    j.boneLengthScale = Vec3(prop.headWidthScale, prop.headScale, prop.headWidthScale);
                    break;
                case HumanoidBoneRole::L_SHOULDER:
                case HumanoidBoneRole::R_SHOULDER:
                    j.boneLengthScale = Vec3(prop.shoulderScale, prop.torsoScale, 1.0f);
                    break;
                case HumanoidBoneRole::L_ARM:
                case HumanoidBoneRole::R_ARM:
                    j.boneLengthScale = Vec3(prop.shoulderScale, prop.shoulderScale, prop.shoulderScale);
                    break;
                case HumanoidBoneRole::L_FOREARM:
                case HumanoidBoneRole::L_HAND:
                case HumanoidBoneRole::R_FOREARM:
                case HumanoidBoneRole::R_HAND:
                    j.boneLengthScale = Vec3(prop.armScale, prop.armScale, prop.armScale);
                    break;
                case HumanoidBoneRole::L_UPLEG:
                case HumanoidBoneRole::R_UPLEG:
                    j.boneLengthScale = Vec3(prop.hipScale, prop.legScale, 1.0f);
                    break;
                case HumanoidBoneRole::L_LEG:
                case HumanoidBoneRole::L_FOOT:
                case HumanoidBoneRole::L_TOE:
                case HumanoidBoneRole::R_LEG:
                case HumanoidBoneRole::R_FOOT:
                case HumanoidBoneRole::R_TOE:
                    j.boneLengthScale = Vec3(prop.legScale, prop.legScale, prop.legScale);
                    break;
                default:
                    j.boneLengthScale = Vec3(1.0f, 1.0f, 1.0f);
                    break;
            }
        }

        // 2. Measure adapted skeleton bounds & normalize exact world height [0 .. targetH]
        inst->rootNormalizeScale = Vec3(1, 1, 1);
        inst->rootGroundOffset = Vec3(0, 0, 0);
        inst->EvaluateRestPoseGlobals();

        auto jointWorldPos = [&](HumanoidBoneRole r, const Vec3& fb) -> Vec3 {
            int idx = FindJointByRole(*inst, r);
            return (idx >= 0) ? inst->joints[idx].globalMatrix.TransformPoint(Vec3(0, 0, 0)) : fb;
        };
        float yHeadTop = jointWorldPos(HumanoidBoneRole::HEAD_TOP, Vec3(0, targetH, 0)).y;
        if (FindJointByRole(*inst, HumanoidBoneRole::HEAD_TOP) < 0) {
            yHeadTop = jointWorldPos(HumanoidBoneRole::HEAD, Vec3(0, targetH * 0.92f, 0)).y * 1.09f;
        }
        float yFootL = jointWorldPos(HumanoidBoneRole::L_FOOT, Vec3(0, 0, 0)).y;
        float yFootR = jointWorldPos(HumanoidBoneRole::R_FOOT, Vec3(0, 0, 0)).y;
        float minFootY = std::min(yFootL, yFootR);
        float rawSpanY = std::max(0.10f, yHeadTop - minFootY);
        float scaleToMeters = (targetH * 0.96f) / rawSpanY; // 4% room for boot sole & hair crown

        // Shift Hips horizontally & depth to center at X=0, Z=0 while placing soles cleanly at Y=0.04*targetH
        Vec3 hipsPos = jointWorldPos(HumanoidBoneRole::HIPS, Vec3(0, targetH * 0.5f, 0));
        inst->rootNormalizeScale = Vec3(scaleToMeters, scaleToMeters, scaleToMeters);
        inst->rootGroundOffset = Vec3(
            -hipsPos.x * scaleToMeters,
            (0.035f * targetH) - minFootY * scaleToMeters,
            -hipsPos.z * scaleToMeters
        );

        inst->EvaluateRestPoseGlobals();
        inst->RecomputeSkinIBMsFromCurrentGlobals();

        // 3. Populate Retargeted GLB Dances + Idol/Idle clips cleanly in adapted skeleton rest pose
        for (const auto& ex : cache.extraRetargetedClips) {
            inst->clips.push_back(ex);
        }
        auto idolClip = BuildIdolGrooveClip(*inst, animSeed);
        auto hopClip = BuildBunnyHopClip(*inst, animSeed);
        auto idleClip = BuildIdleBreatheClip(*inst, animSeed);
        if (!preferNativeClipFirst) {
            inst->clips.insert(inst->clips.begin(), idolClip);
            inst->clips.push_back(hopClip);
            inst->clips.push_back(idleClip);
        } else {
            inst->clips.push_back(idolClip);
            inst->clips.push_back(hopClip);
            inst->clips.push_back(idleClip);
        }
        return inst;
    }

    // ---- Closed 360° Multi-Part Articulated Costume Mesh Builder ----
    struct TubeRingSample {
        Vec3 center;
        float rU = 0.05f; // along U (front/back or lateral normal)
        float rV = 0.05f; // along V (width)
        float vTex = 0.5f;
        float uSpanL = 0.35f;
        float uSpanR = 0.65f;
        uint16_t j0 = 0, j1 = 0;
        float w0 = 1.0f, w1 = 0.0f;
        bool sculptHumanProfile = false; // subtle chest & spinal relief on torso
        bool isHeadCranium = false;      // soft front facial plane + fuller hair occiput
        bool useFixedAtlasSwatch = false;
        float fixedU = 0.25f;
        float fixedV = 0.25f;
        float flatBottomY = -1e9f;       // optional flat outsole clamp for shoes/boots
    };

    static void AppendClosedLoftTube(
        SurrealMesh& mesh,
        std::vector<SkinnedVertex3D>& bindVerts,
        const CleanCostumeAtlas& atlas,
        const Vec3& axisUpHint,
        const std::vector<TubeRingSample>& rings,
        int radialSegs,
        bool faceFromFrontZ = true)
    {
        if (rings.size() < 2 || radialSegs < 8) return;
        const float PI = 3.14159265359f;
        const size_t baseVertOffset = mesh.vertices.size();
        const int R = static_cast<int>(rings.size());
        const int halfSegs = std::max(4, radialSegs / 2);
        const int arcVerts = halfSegs + 1;
        const int ringStride = arcVerts * 2; // [0..halfSegs]=Front Arc, [halfSegs+1..ringStride-1]=Back Arc

        auto pushVert = [&](const Vec3& p, const Vec3& n, float u, float v, const TubeRingSample& rs) {
            ColorRGB c = SampleAtlasColor(atlas, u, v);
            Vec4 vc(c.r, c.g, c.b, 1.0f);
            mesh.vertices.push_back(Vertex3D(p, n, u, v, vc));

            SkinnedVertex3D sv;
            sv.position = p;
            sv.normal = n;
            sv.u = u;
            sv.v = v;
            sv.color = vc;
            sv.joints[0] = rs.j0;
            sv.joints[1] = rs.j1;
            sv.weights[0] = rs.w0;
            sv.weights[1] = rs.w1;
            bindVerts.push_back(sv);
        };

        // Compute stable right-handed orthonormal frames (U, V, T) with U x V = T
        // Split each ring cleanly into Front [-pi/2..+pi/2] and Back [+pi/2..+3pi/2] arcs
        // with duplicated seam vertices so ZERO triangles ever span across the 0.50 atlas boundary!
        for (int k = 0; k < R; ++k) {
            Vec3 T;
            if (k == 0) T = (rings[1].center - rings[0].center);
            else if (k == R - 1) T = (rings[R - 1].center - rings[R - 2].center);
            else T = (rings[k + 1].center - rings[k - 1].center);
            T = (T.LengthSq() > 1e-8f) ? T.Normalized() : Vec3(0, 1, 0);

            Vec3 U_base = axisUpHint;
            if (std::abs(U_base.Dot(T)) > 0.92f) U_base = Vec3(0, 0, 1);
            if (std::abs(U_base.Dot(T)) > 0.92f) U_base = Vec3(0, 1, 0);
            if (std::abs(U_base.Dot(T)) > 0.92f) U_base = Vec3(1, 0, 0);
            Vec3 V = T.Cross(U_base).Normalized();
            Vec3 U = V.Cross(T).Normalized(); // U x V = T; U aligns with Front (+Z)

            bool orientVxRight = (std::abs(V.x) >= 0.35f && V.x < 0.0f);
            const auto& rs = rings[k];

            auto emitVertex = [&](float phi, bool isFrontHemisphere) {
                float cP = std::cos(phi);
                float sP = std::sin(phi);

                float radU = rs.rU;
                if (rs.sculptHumanProfile) {
                    if (cP > 0.0f) radU *= (1.0f + 0.08f * std::pow(std::abs(sP), 0.7f) * cP);
                    else radU *= (0.94f + 0.08f * std::exp(-std::pow(std::abs(sP) - 0.45f, 2.0f) / 0.12f));
                } else if (rs.isHeadCranium) {
                    if (cP > 0.0f) {
                        // Gently planarize front facial cheeks/eyes so normals reflect light cleanly + subtle nose ridge
                        radU *= (0.76f + 0.09f * std::exp(-(sP * sP) / 0.045f));
                    } else {
                        radU *= 1.06f;
                    }
                }

                Vec3 offset = U * (radU * cP) + V * (rs.rV * sP);
                Vec3 pos = rs.center + offset;
                if (rs.flatBottomY > -1e8f && pos.y < rs.flatBottomY) {
                    pos.y = rs.flatBottomY;
                }
                Vec3 outN = (U * (cP / std::max(1e-4f, radU)) + V * (sP / std::max(1e-4f, rs.rV))).Normalized();

                float uTex = rs.fixedU;
                float vTex = rs.vTex;
                if (rs.useFixedAtlasSwatch) {
                    vTex = rs.fixedV;
                } else {
                    float horizSign = faceFromFrontZ ? (orientVxRight ? -sP : sP) : sP;
                    float sideFrac = 0.5f + 0.5f * horizSign;
                    float horizCoord = rs.uSpanL + std::clamp(sideFrac, 0.01f, 0.99f) * (rs.uSpanR - rs.uSpanL);
                    uTex = isFrontHemisphere ? (0.002f + horizCoord * 0.494f)
                                             : (0.504f + horizCoord * 0.494f);
                }
                pushVert(pos, outN, uTex, vTex, rs);
            };

            for (int s = 0; s < arcVerts; ++s) {
                float phi = -0.5f * PI + (static_cast<float>(s) / halfSegs) * PI;
                emitVertex(phi, true);
            }
            for (int s = 0; s < arcVerts; ++s) {
                float phi = 0.5f * PI + (static_cast<float>(s) / halfSegs) * PI;
                emitVertex(phi, false);
            }
        }

        // Stitch Front and Back hemispheres separately (CCW outward when U x V = T)
        for (int k = 0; k + 1 < R; ++k) {
            uint32_t rowA = static_cast<uint32_t>(baseVertOffset + k * ringStride);
            uint32_t rowB = static_cast<uint32_t>(baseVertOffset + (k + 1) * ringStride);
            for (int half = 0; half < 2; ++half) {
                uint32_t hOff = static_cast<uint32_t>(half * arcVerts);
                for (int s = 0; s < halfSegs; ++s) {
                    uint32_t a = rowA + hOff + s;
                    uint32_t b = rowA + hOff + s + 1;
                    uint32_t c = rowB + hOff + s + 1;
                    uint32_t d = rowB + hOff + s;
                    mesh.indices.push_back(a); mesh.indices.push_back(b); mesh.indices.push_back(c);
                    mesh.indices.push_back(a); mesh.indices.push_back(c); mesh.indices.push_back(d);
                }
            }
        }
        size_t tubeTriStart = 0;
        size_t tubeTriEnd = mesh.indices.size();
        tubeTriStart = tubeTriEnd - static_cast<size_t>(R - 1) * halfSegs * 2 * 6;

        // Watertight Hemisphere-Separated Bottom & Top End Caps (zero UV seam crossing!)
        Vec3 T_bot = (rings[1].center - rings[0].center);
        T_bot = (T_bot.LengthSq() > 1e-8f) ? (T_bot.Normalized() * -1.0f) : Vec3(0, -1, 0);
        float midU_bot = 0.5f * (rings[0].uSpanL + rings[0].uSpanR);
        float botFrontU = rings[0].useFixedAtlasSwatch ? rings[0].fixedU : (0.002f + midU_bot * 0.494f);
        float botBackU  = rings[0].useFixedAtlasSwatch ? rings[0].fixedU : (0.504f + midU_bot * 0.494f);
        float botV      = rings[0].useFixedAtlasSwatch ? rings[0].fixedV : rings[0].vTex;
        Vec3 botCenterPos = rings[0].center;
        if (rings[0].flatBottomY > -1e8f && botCenterPos.y < rings[0].flatBottomY) botCenterPos.y = rings[0].flatBottomY;
        uint32_t botFrontCenter = static_cast<uint32_t>(mesh.vertices.size());
        pushVert(botCenterPos, T_bot, botFrontU, botV, rings[0]);
        uint32_t botBackCenter = static_cast<uint32_t>(mesh.vertices.size());
        pushVert(botCenterPos, T_bot, botBackU, botV, rings[0]);
        for (int s = 0; s < halfSegs; ++s) {
            mesh.indices.push_back(botFrontCenter);
            mesh.indices.push_back(static_cast<uint32_t>(baseVertOffset + s + 1));
            mesh.indices.push_back(static_cast<uint32_t>(baseVertOffset + s));

            uint32_t bOff = static_cast<uint32_t>(baseVertOffset + arcVerts);
            mesh.indices.push_back(botBackCenter);
            mesh.indices.push_back(bOff + s + 1);
            mesh.indices.push_back(bOff + s);
        }

        Vec3 T_top = (rings[R - 1].center - rings[R - 2].center);
        T_top = (T_top.LengthSq() > 1e-8f) ? T_top.Normalized() : Vec3(0, 1, 0);
        float midU_top = 0.5f * (rings[R - 1].uSpanL + rings[R - 1].uSpanR);
        float topFrontU = rings[R - 1].useFixedAtlasSwatch ? rings[R - 1].fixedU : (0.002f + midU_top * 0.494f);
        float topBackU  = rings[R - 1].useFixedAtlasSwatch ? rings[R - 1].fixedU : (0.504f + midU_top * 0.494f);
        float topV      = rings[R - 1].useFixedAtlasSwatch ? rings[R - 1].fixedV : rings[R - 1].vTex;
        Vec3 topCenterPos = rings[R - 1].center;
        if (rings[R - 1].flatBottomY > -1e8f && topCenterPos.y < rings[R - 1].flatBottomY) topCenterPos.y = rings[R - 1].flatBottomY;
        uint32_t topFrontCenter = static_cast<uint32_t>(mesh.vertices.size());
        pushVert(topCenterPos, T_top, topFrontU, topV, rings[R - 1]);
        uint32_t topBackCenter = static_cast<uint32_t>(mesh.vertices.size());
        pushVert(topCenterPos, T_top, topBackU, topV, rings[R - 1]);
        uint32_t topRow = static_cast<uint32_t>(baseVertOffset + (R - 1) * ringStride);
        for (int s = 0; s < halfSegs; ++s) {
            mesh.indices.push_back(topFrontCenter);
            mesh.indices.push_back(topRow + s);
            mesh.indices.push_back(topRow + s + 1);

            uint32_t tOff = topRow + arcVerts;
            mesh.indices.push_back(topBackCenter);
            mesh.indices.push_back(tOff + s);
            mesh.indices.push_back(tOff + s + 1);
        }

        // Accumulate smooth vertex normals over tube quads & merge across split seam vertices
        size_t ringVertEnd = baseVertOffset + static_cast<size_t>(R) * ringStride;
        std::vector<Vec3> acc(ringVertEnd - baseVertOffset, Vec3(0, 0, 0));
        for (size_t t = tubeTriStart; t + 2 < tubeTriEnd; t += 3) {
            uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t + 1], i2 = mesh.indices[t + 2];
            Vec3 fn = (mesh.vertices[i1].position - mesh.vertices[i0].position).Cross(
                       mesh.vertices[i2].position - mesh.vertices[i0].position);
            if (i0 >= baseVertOffset && i0 < ringVertEnd) acc[i0 - baseVertOffset] = acc[i0 - baseVertOffset] + fn;
            if (i1 >= baseVertOffset && i1 < ringVertEnd) acc[i1 - baseVertOffset] = acc[i1 - baseVertOffset] + fn;
            if (i2 >= baseVertOffset && i2 < ringVertEnd) acc[i2 - baseVertOffset] = acc[i2 - baseVertOffset] + fn;
        }
        // Merge seam pairs: Front[0]==Back[halfSegs] (-pi/2) and Front[halfSegs]==Back[0] (+pi/2)
        for (int k = 0; k < R; ++k) {
            size_t rOff = static_cast<size_t>(k) * ringStride;
            Vec3 nM = acc[rOff + 0] + acc[rOff + arcVerts + halfSegs];
            acc[rOff + 0] = acc[rOff + arcVerts + halfSegs] = nM;
            Vec3 nP = acc[rOff + halfSegs] + acc[rOff + arcVerts + 0];
            acc[rOff + halfSegs] = acc[rOff + arcVerts + 0] = nP;
        }
        for (size_t i = baseVertOffset; i < ringVertEnd; ++i) {
            const Vec3& nAcc = acc[i - baseVertOffset];
            if (nAcc.LengthSq() > 1e-8f) {
                Vec3 nN = nAcc.Normalized();
                mesh.vertices[i].normal = nN;
                bindVerts[i].normal = nN;
            }
        }
    }

    static std::shared_ptr<SurrealMesh> BuildCostumeMeshOverSkeleton(
        const SkeletalMeshInstance& skel,
        CleanCostumeAtlas& atlas,
        const SubjectProportions& prop,
        float H)
    {
        auto mesh = std::make_shared<SurrealMesh>();
        atlas.generatedBindVerts.clear();

        auto slotOf = [&](HumanoidBoneRole r, int fallbackSlot = 0) -> uint16_t {
            int j = FindJointByRole(skel, r);
            int s = FindSkinSlotForJoint(skel, j);
            return static_cast<uint16_t>(s >= 0 ? s : fallbackSlot);
        };
        auto posOf = [&](HumanoidBoneRole r, const Vec3& fb) -> Vec3 {
            int j = FindJointByRole(skel, r);
            return (j >= 0) ? skel.joints[j].globalMatrix.TransformPoint(Vec3(0, 0, 0)) : fb;
        };

        uint16_t sHips = slotOf(HumanoidBoneRole::HIPS, 0);
        uint16_t sSpine = slotOf(HumanoidBoneRole::SPINE, sHips);
        uint16_t sSpine1 = slotOf(HumanoidBoneRole::SPINE1, sSpine);
        uint16_t sSpine2 = slotOf(HumanoidBoneRole::SPINE2, sSpine1);
        uint16_t sNeck = slotOf(HumanoidBoneRole::NECK, sSpine2);
        uint16_t sHead = slotOf(HumanoidBoneRole::HEAD, sNeck);

        uint16_t sShL = slotOf(HumanoidBoneRole::L_SHOULDER, sSpine2);
        uint16_t sArmL = slotOf(HumanoidBoneRole::L_ARM, sShL);
        uint16_t sFArmL = slotOf(HumanoidBoneRole::L_FOREARM, sArmL);
        uint16_t sHandL = slotOf(HumanoidBoneRole::L_HAND, sFArmL);

        uint16_t sShR = slotOf(HumanoidBoneRole::R_SHOULDER, sSpine2);
        uint16_t sArmR = slotOf(HumanoidBoneRole::R_ARM, sShR);
        uint16_t sFArmR = slotOf(HumanoidBoneRole::R_FOREARM, sArmR);
        uint16_t sHandR = slotOf(HumanoidBoneRole::R_HAND, sFArmR);

        uint16_t sUpLegL = slotOf(HumanoidBoneRole::L_UPLEG, sHips);
        uint16_t sLegL = slotOf(HumanoidBoneRole::L_LEG, sUpLegL);
        uint16_t sFootL = slotOf(HumanoidBoneRole::L_FOOT, sLegL);
        uint16_t sToeL  = slotOf(HumanoidBoneRole::L_TOE, sFootL);

        uint16_t sUpLegR = slotOf(HumanoidBoneRole::R_UPLEG, sHips);
        uint16_t sLegR = slotOf(HumanoidBoneRole::R_LEG, sUpLegR);
        uint16_t sFootR = slotOf(HumanoidBoneRole::R_FOOT, sLegR);
        uint16_t sToeR  = slotOf(HumanoidBoneRole::R_TOE, sFootR);

        int jHandLIdx = FindJointByRole(skel, HumanoidBoneRole::L_HAND);
        int jHandRIdx = FindJointByRole(skel, HumanoidBoneRole::R_HAND);

        Vec3 pHips = posOf(HumanoidBoneRole::HIPS, Vec3(0, 0.50f * H, 0));
        Vec3 pSpine = posOf(HumanoidBoneRole::SPINE, Vec3(0, 0.58f * H, 0));
        Vec3 pSpine2 = posOf(HumanoidBoneRole::SPINE2, Vec3(0, 0.72f * H, 0));
        Vec3 pNeck = posOf(HumanoidBoneRole::NECK, Vec3(0, 0.82f * H, 0));
        Vec3 pHead = posOf(HumanoidBoneRole::HEAD, Vec3(0, 0.87f * H, 0));
        Vec3 pHeadTop = posOf(HumanoidBoneRole::HEAD_TOP, Vec3(0, H, 0));
        if (FindJointByRole(skel, HumanoidBoneRole::HEAD_TOP) < 0) pHeadTop = Vec3(pHead.x, H, pHead.z);

        Vec3 pArmL = posOf(HumanoidBoneRole::L_ARM, Vec3(prop.shoulderHalfWRatio * H, pNeck.y - 0.03f * H, 0));
        Vec3 pFArmL = posOf(HumanoidBoneRole::L_FOREARM, pArmL + Vec3(0.14f * H, 0, 0));
        Vec3 pHandL = posOf(HumanoidBoneRole::L_HAND, pFArmL + Vec3(0.12f * H, 0, 0));

        Vec3 pArmR = posOf(HumanoidBoneRole::R_ARM, Vec3(-prop.shoulderHalfWRatio * H, pNeck.y - 0.03f * H, 0));
        Vec3 pFArmR = posOf(HumanoidBoneRole::R_FOREARM, pArmR - Vec3(0.14f * H, 0, 0));
        Vec3 pHandR = posOf(HumanoidBoneRole::R_HAND, pFArmR - Vec3(0.12f * H, 0, 0));

        Vec3 pUpLegL = posOf(HumanoidBoneRole::L_UPLEG, Vec3(prop.hipHalfWRatio * 0.48f * H, pHips.y - 0.03f * H, 0));
        Vec3 pLegL = posOf(HumanoidBoneRole::L_LEG, Vec3(pUpLegL.x, pUpLegL.y * 0.52f, 0));
        Vec3 pFootL = posOf(HumanoidBoneRole::L_FOOT, Vec3(pUpLegL.x, 0.04f * H, 0));
        Vec3 pToeL = posOf(HumanoidBoneRole::L_TOE, pFootL + Vec3(0, -0.02f * H, 0.08f * H));

        Vec3 pUpLegR = posOf(HumanoidBoneRole::R_UPLEG, Vec3(-prop.hipHalfWRatio * 0.48f * H, pHips.y - 0.03f * H, 0));
        Vec3 pLegR = posOf(HumanoidBoneRole::R_LEG, Vec3(pUpLegR.x, pUpLegR.y * 0.52f, 0));
        Vec3 pFootR = posOf(HumanoidBoneRole::R_FOOT, Vec3(pUpLegR.x, 0.04f * H, 0));
        Vec3 pToeR = posOf(HumanoidBoneRole::R_TOE, pFootR + Vec3(0, -0.02f * H, 0.08f * H));

        auto sliceAtYFrac = [&](float yF) -> const SilhouetteSlice& {
            int idx = std::clamp(static_cast<int>(yF * (prop.slices.size() - 1)), 0, static_cast<int>(prop.slices.size()) - 1);
            return prop.slices[idx];
        };
        float figAspect = static_cast<float>(atlas.singleW) / std::max(1.0f, static_cast<float>(atlas.texH));

        // =========================================================================
        // 1. TORSO / JACKET / DRESS LOFT (Hips bottom -> Spine -> Spine2 -> Neck)
        // =========================================================================
        {
            const int TORSO_RINGS = 24;
            std::vector<TubeRingSample> rings(TORSO_RINGS);
            float torsoStartYFrac = prop.hasSkirtOrCoat ? prop.skirtBottomFrac : (prop.crotchFrac + 0.01f);
            float torsoEndYFrac = prop.headEndFrac + 0.002f;
            Vec3 botCenter = Vec3(pHips.x, std::min(pUpLegL.y, pUpLegR.y) - (prop.hasSkirtOrCoat ? 0.08f * H : 0.02f * H), pHips.z);
            botCenter.y = std::max(0.08f * H, botCenter.y);

            for (int k = 0; k < TORSO_RINGS; ++k) {
                float t = static_cast<float>(k) / (TORSO_RINGS - 1);
                float yF = torsoStartYFrac + t * (torsoEndYFrac - torsoStartYFrac);
                const auto& sl = sliceAtYFrac(yF);
                auto& rs = rings[k];

                // Interpolate center smoothly along Spine curve
                if (t < 0.35f) {
                    float a = t / 0.35f;
                    rs.center = botCenter * (1.0f - a) + pSpine * a;
                    rs.j0 = sHips; rs.j1 = sSpine; rs.w0 = 1.0f - a * 0.5f; rs.w1 = a * 0.5f;
                } else if (t < 0.78f) {
                    float a = (t - 0.35f) / 0.43f;
                    rs.center = pSpine * (1.0f - a) + pSpine2 * a;
                    rs.j0 = sSpine1; rs.j1 = sSpine2; rs.w0 = 1.0f - a; rs.w1 = a;
                } else {
                    float a = (t - 0.78f) / 0.22f;
                    rs.center = pSpine2 * (1.0f - a) + pNeck * a;
                    rs.j0 = sSpine2; rs.j1 = sNeck; rs.w0 = 1.0f - a; rs.w1 = a;
                }

                float halfW = std::clamp(0.5f * (sl.torsoR - sl.torsoL) * figAspect * H, 0.055f * H, 0.23f * H);
                float shReach = std::max(std::abs(pArmL.x - pNeck.x), std::abs(pArmR.x - pNeck.x));
                if (t > 0.65f) {
                    // Smooth rounded trapezius slope from chest/armpit into shoulder socket and collarbone
                    float shBlend = std::clamp((t - 0.65f) / 0.14f, 0.0f, 1.0f);
                    float nt = std::clamp((t - 0.76f) / 0.24f, 0.0f, 1.0f);
                    float neckT = nt * nt * (3.0f - 2.0f * nt);
                    float bridgeW = shReach * (0.94f - 0.50f * neckT);
                    halfW = std::max(halfW * (1.0f - neckT * 0.62f), bridgeW * shBlend);
                }
                float depthRatio = 0.66f;
                if (t > 0.25f && t < 0.55f) depthRatio = 0.60f; // slight waist curve
                else if (t > 0.55f && t < 0.82f) depthRatio = 0.70f; // ribcage/chest volume
                else if (t >= 0.82f) depthRatio = 0.78f;

                rs.rV = halfW;
                rs.rU = std::max(0.045f * H, halfW * depthRatio);
                rs.vTex = std::clamp(yF, 0.02f, 0.98f);
                rs.uSpanL = sl.torsoL;
                rs.uSpanR = sl.torsoR;
                rs.sculptHumanProfile = true;
            }
            AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 0, 1), rings, 32, true);
        }

        // =========================================================================
        // 2. SEPARATE CYLINDRICAL NECK + SCULPTED ANATOMICAL HEAD / PLANAR FACE
        // =========================================================================
        {
            float chinFrac = std::clamp(prop.faceBottomFrac, 0.130f, prop.headEndFrac - 0.005f);
            Vec3 yJawPos(
                0.35f * pNeck.x + 0.65f * pHead.x,
                std::max(pNeck.y + 0.015f * H, pHead.y - 0.022f * H),
                0.45f * pNeck.z + 0.55f * pHead.z
            );
            Vec3 neckStart = pNeck - Vec3(0, 0.032f * H, 0);

            // 2A. Sleek anatomical neck column (collar -> jaw base)
            {
                const int NECK_RINGS = 7;
                std::vector<TubeRingSample> neckRings(NECK_RINGS);
                for (int k = 0; k < NECK_RINGS; ++k) {
                    float t = static_cast<float>(k) / (NECK_RINGS - 1);
                    auto& rs = neckRings[k];
                    rs.center = neckStart * (1.0f - t) + (yJawPos + Vec3(0, 0.015f * H, 0)) * t;
                    float r = (0.034f - 0.004f * t) * H;
                    rs.rV = r;
                    rs.rU = r * 0.96f;
                    float yF = prop.headEndFrac * (1.0f - t) + chinFrac * t;
                    rs.vTex = std::clamp(yF, 0.05f, 0.25f);
                    rs.uSpanL = std::clamp(prop.faceCenterU - 0.055f, 0.02f, 0.90f);
                    rs.uSpanR = std::clamp(prop.faceCenterU + 0.055f, rs.uSpanL + 0.03f, 0.98f);
                    rs.j0 = sNeck; rs.j1 = sHead;
                    rs.w0 = 1.0f - t * 0.65f; rs.w1 = 1.0f - rs.w0;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 0, 1), neckRings, 20, true);
            }

            // 2B. Sculpted oval cranium with gentle forward facial plane & full occipital hair dome
            {
                const int HEAD_RINGS = 26;
                std::vector<TubeRingSample> rings(HEAD_RINGS);
                Vec3 yCrownPos(pHeadTop.x, std::max(yJawPos.y + 0.125f * H, pHeadTop.y + 0.012f * H), 0.55f * pHead.z + 0.45f * pHeadTop.z);
                float baseRx = std::clamp(prop.headHalfWRatio * H, 0.056f * H, 0.115f * H);
                float baseRz = baseRx * 1.12f;

                for (int k = 0; k < HEAD_RINGS; ++k) {
                    float t = static_cast<float>(k) / (HEAD_RINGS - 1);
                    auto& rs = rings[k];

                    // Subtle forward face curve across forehead & brow
                    float zArc = yJawPos.z * (1.0f - t) + yCrownPos.z * t + std::sin(t * 3.14159f) * (0.010f * H);
                    rs.center = Vec3(
                        yJawPos.x * (1.0f - t) + yCrownPos.x * t,
                        yJawPos.y * (1.0f - t) + yCrownPos.y * t,
                        zArc
                    );

                    float profW = 1.0f, profD = 1.0f;
                    if (t < 0.18f) {
                        // Tapered jawline -> chin
                        float a = t / 0.18f;
                        float s = a * a * (3.0f - 2.0f * a);
                        profW = 0.36f + 0.50f * s;
                        profD = 0.40f + 0.48f * s;
                    } else if (t < 0.68f) {
                        // Cheekbones & temples
                        float a = (t - 0.18f) / 0.50f;
                        profW = 0.86f + 0.14f * std::sin(a * 3.14159f * 0.72f);
                        profD = 0.88f + 0.12f * std::sin(a * 3.14159f * 0.75f);
                    } else {
                        // Cranial dome to hair crown
                        float a = (t - 0.68f) / 0.32f;
                        profW = std::sqrt(std::max(0.03f, 1.0f - a * a * 0.94f));
                        profD = profW;
                        if (k == HEAD_RINGS - 1) { profW = 0.18f; profD = 0.18f; }
                    }

                    rs.rV = std::max(0.010f * H, baseRx * profW);
                    rs.rU = std::max(0.011f * H, baseRz * profD);
                    // Linear height->V across facial portrait box so eyes, nose & lips align without distortion!
                    rs.vTex = std::clamp(chinFrac * (1.0f - t) + 0.008f * t, 0.008f, chinFrac);
                    rs.uSpanL = prop.faceU0;
                    rs.uSpanR = prop.faceU1;
                    rs.j0 = sHead; rs.j1 = sNeck; rs.w0 = (t < 0.12f) ? (0.75f + t * 2.0f) : 1.0f; rs.w1 = 1.0f - rs.w0;
                    rs.isHeadCranium = true;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 0, 1), rings, 36, true);
            }
        }

        // =========================================================================
        // 3. ARTICULATED ARMS + SCULPTED 3D HANDS (PALM, ANGLED THUMB & 4 FINGERS)
        // =========================================================================
        auto findChildPosAndSlot = [&](int parentIdx, const char* key, const Vec3& fbPos, uint16_t fbSlot, uint16_t& outSlot) -> Vec3 {
            outSlot = fbSlot;
            if (parentIdx < 0) return fbPos;
            for (size_t i = 0; i < skel.joints.size(); ++i) {
                if (skel.joints[i].parentIndex != parentIdx) continue;
                std::string low = skel.joints[i].name;
                for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (low.find(key) != std::string::npos) {
                    int s = FindSkinSlotForJoint(skel, static_cast<int>(i));
                    if (s >= 0) outSlot = static_cast<uint16_t>(s);
                    return skel.joints[i].globalMatrix.TransformPoint(Vec3(0, 0, 0));
                }
            }
            return fbPos;
        };

        auto buildArmAndHand = [&](bool isLeft, int jHandIdx, Vec3 pShStart, Vec3 pElbow, Vec3 pWrist,
                                   uint16_t jSh, uint16_t jUpArm, uint16_t jFArm, uint16_t jHand)
        {
            // 3A. Anatomical Sleeve / Forearm ending cleanly at the wrist cuff
            const int ARM_RINGS = 16;
            std::vector<TubeRingSample> rings(ARM_RINGS);
            Vec3 pStart(0.55f * pNeck.x + 0.45f * pShStart.x, 0.88f * pShStart.y + 0.12f * pSpine2.y - 0.008f * H, 0.45f * pNeck.z + 0.55f * pShStart.z);
            float baseArmRad = std::clamp(prop.shoulderHalfWRatio * 0.22f * H, 0.025f * H, 0.044f * H);

            for (int k = 0; k < ARM_RINGS; ++k) {
                float t = static_cast<float>(k) / (ARM_RINGS - 1);
                auto& rs = rings[k];
                if (t < 0.50f) {
                    float a = t / 0.50f;
                    rs.center = pStart * (1.0f - a) + pElbow * a;
                    rs.j0 = (a < 0.28f) ? jSh : jUpArm;
                    rs.j1 = (a < 0.28f) ? jUpArm : jFArm;
                    rs.w0 = (a < 0.28f) ? (1.0f - a * 1.8f) : (1.0f - a * 0.35f);
                    rs.w1 = 1.0f - rs.w0;
                } else {
                    float a = (t - 0.50f) / 0.50f;
                    rs.center = pElbow * (1.0f - a) + pWrist * a;
                    rs.j0 = jFArm; rs.j1 = jHand;
                    rs.w0 = (a < 0.82f) ? 1.0f : (1.0f - (a - 0.82f) * 3.2f);
                    rs.w1 = 1.0f - rs.w0;
                }

                // Smooth torso socket insertion -> rounded deltoid swell -> biceps -> elbow taper -> forearm -> wrist
                float radFrac = 1.0f;
                if (t < 0.16f) radFrac = 0.76f + 0.32f * std::sin((t / 0.16f) * 1.5708f);
                else if (t < 0.50f) radFrac = 1.00f - 0.20f * ((t - 0.16f) / 0.34f);
                else if (t < 0.72f) radFrac = 0.80f + 0.06f * std::sin(((t - 0.50f) / 0.22f) * 3.14159f);
                else radFrac = 0.80f - 0.22f * ((t - 0.72f) / 0.28f);

                float r = std::max(0.015f * H, baseArmRad * radFrac);
                rs.rU = r;
                rs.rV = r * 0.94f;

                float sampleYFrac = std::clamp(prop.shoulderFrac + t * (prop.waistFrac - prop.shoulderFrac) * 0.85f, 0.18f, 0.44f);
                const auto& sl = sliceAtYFrac(sampleYFrac);
                float fullW = std::max(0.06f, sl.uRight - sl.uLeft);
                if (isLeft) {
                    rs.uSpanR = std::clamp(sl.uRight - 0.05f * fullW, 0.05f, 0.95f);
                    rs.uSpanL = std::clamp(sl.uRight - 0.18f * fullW, 0.03f, rs.uSpanR - 0.02f);
                } else {
                    rs.uSpanL = std::clamp(sl.uLeft + 0.05f * fullW, 0.04f, 0.94f);
                    rs.uSpanR = std::clamp(sl.uLeft + 0.18f * fullW, rs.uSpanL + 0.02f, 0.96f);
                }
                rs.vTex = sampleYFrac;
            }
            AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 0, 1), rings, 20, false);

            // 3B. Real Sculpted 3D Human Hand (Wrist Collar -> Broad Flat Palm -> Angled Thumb -> 4 Fingers)
            uint16_t sIndex = jHand, sThumb = jHand;
            Vec3 forearmDir = (pWrist - pElbow);
            Vec3 fbIndex = pWrist + ((forearmDir.LengthSq() > 1e-6f) ? forearmDir.Normalized() : Vec3(isLeft ? 1.0f : -1.0f, 0, 0)) * (0.05f * H);
            Vec3 pIdx1 = findChildPosAndSlot(jHandIdx, "index", fbIndex, jHand, sIndex);
            findChildPosAndSlot(jHandIdx, "thumb", fbIndex, jHand, sThumb);

            Vec3 handFwd = (pIdx1 - pWrist);
            if (handFwd.LengthSq() < 1e-6f) handFwd = (pWrist - pElbow);
            handFwd = (handFwd.LengthSq() > 1e-6f) ? handFwd.Normalized() : Vec3(isLeft ? 1.0f : -1.0f, 0, 0);

            Vec3 upRef = (std::abs(handFwd.y) < 0.85f) ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
            Vec3 toThumb = handFwd.Cross(upRef).Normalized();
            if (toThumb.z < 0.0f && std::abs(handFwd.z) < 0.70f) toThumb = toThumb * -1.0f;
            Vec3 handBack = toThumb.Cross(handFwd).Normalized();
            if (handBack.y < 0.0f && std::abs(handFwd.y) < 0.70f) handBack = handBack * -1.0f;

            // Sculpted Broad Flat Palm (0.82x refined proportions, wrist anchored cleanly)
            {
                const int PALM_RINGS = 7;
                std::vector<TubeRingSample> pRings(PALM_RINGS);
                float palmLen = 0.040f * H;
                for (int k = 0; k < PALM_RINGS; ++k) {
                    float a = static_cast<float>(k) / (PALM_RINGS - 1);
                    auto& rs = pRings[k];
                    rs.center = pWrist + handFwd * (-0.004f * H + a * palmLen);
                    float widen = (k == PALM_RINGS - 1) ? 0.90f : (0.78f + 0.28f * std::sin(a * 2.2f));
                    rs.rU = (0.0094f - 0.0020f * a) * H; // thin flat palm thickness
                    rs.rV = 0.0180f * H * widen;         // refined width across knuckles
                    rs.j0 = jHand; rs.j1 = jFArm;
                    rs.w0 = (k == 0) ? 0.82f : 1.0f; rs.w1 = 1.0f - rs.w0;
                    rs.useFixedAtlasSwatch = true;
                    rs.fixedU = atlas.skinU;
                    rs.fixedV = atlas.skinV;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, handBack, pRings, 14, true);
            }

            // Sculpted Angled Thumb (embedded into palm + base locked 100% to jHand so zero gap)
            {
                const int THUMB_RINGS = 6;
                std::vector<TubeRingSample> tRings(THUMB_RINGS);
                Vec3 tBase = pWrist + handFwd * (0.010f * H) + toThumb * (0.009f * H) - handBack * (0.002f * H);
                Vec3 tDir = (handFwd * 0.74f + toThumb * 0.64f - handBack * 0.16f).Normalized();
                for (int k = 0; k < THUMB_RINGS; ++k) {
                    float a = static_cast<float>(k) / (THUMB_RINGS - 1);
                    auto& rs = tRings[k];
                    rs.center = tBase + tDir * (a * 0.029f * H);
                    float r = (k == THUMB_RINGS - 1) ? (0.0031f * H) : (0.0059f - 0.0018f * a) * H;
                    rs.rU = r; rs.rV = r * 1.06f;
                    rs.j0 = jHand; rs.j1 = sThumb;
                    rs.w0 = (k == 0) ? 1.0f : (1.0f - 0.65f * a);
                    rs.w1 = 1.0f - rs.w0;
                    rs.useFixedAtlasSwatch = true;
                    rs.fixedU = atlas.skinU;
                    rs.fixedV = atlas.skinV;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, handBack, tRings, 10, true);
            }

            // 4 Individual Sculpted Fingers (Index, Middle, Ring, Pinky; 0.82x & anchored inside palm)
            struct FingerSpec { float latOff; float len; float rad; };
            const FingerSpec fingers[4] = {
                { +0.0123f * H, 0.032f * H, 0.0043f * H }, // Index
                { +0.0041f * H, 0.036f * H, 0.0044f * H }, // Middle
                { -0.0043f * H, 0.033f * H, 0.0041f * H }, // Ring
                { -0.0121f * H, 0.025f * H, 0.0036f * H }  // Pinky
            };
            Vec3 fingerDistDir = (handFwd * 0.96f - handBack * 0.28f).Normalized();
            for (const auto& f : fingers) {
                const int F_RINGS = 6;
                std::vector<TubeRingSample> fRings(F_RINGS);
                Vec3 fBase = pWrist + handFwd * (0.033f * H) + toThumb * f.latOff;
                for (int k = 0; k < F_RINGS; ++k) {
                    float a = static_cast<float>(k) / (F_RINGS - 1);
                    auto& rs = fRings[k];
                    Vec3 segDir = (a < 0.45f) ? handFwd : fingerDistDir;
                    rs.center = fBase + handFwd * (std::min(a, 0.45f) * f.len) + segDir * (std::max(0.0f, a - 0.45f) * f.len);
                    float r = f.rad * ((k == F_RINGS - 1) ? 0.55f : (1.0f - 0.28f * a));
                    rs.rU = r; rs.rV = r;
                    rs.j0 = jHand; rs.j1 = sIndex;
                    rs.w0 = (k == 0) ? 1.0f : (1.0f - 0.30f * a);
                    rs.w1 = 1.0f - rs.w0;
                    rs.useFixedAtlasSwatch = true;
                    rs.fixedU = atlas.skinU;
                    rs.fixedV = atlas.skinV;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, handBack, fRings, 8, true);
            }
        };
        buildArmAndHand(true,  jHandLIdx, pArmL, pFArmL, pHandL, sShL, sArmL, sFArmL, sHandL);
        buildArmAndHand(false, jHandRIdx, pArmR, pFArmR, pHandR, sShR, sArmR, sFArmR, sHandR);

        // =========================================================================
        // 4. SLEEK ARTICULATED LEGS + SCULPTED FLAT-SOLED 3D SHOES / BOOTS
        // =========================================================================
        auto buildLegAndShoe = [&](bool isLeft, Vec3 pHipTop, Vec3 pKnee, Vec3 pAnkle, Vec3 pToePos,
                                   uint16_t jUpLeg, uint16_t jLeg, uint16_t jFoot, uint16_t jToe)
        {
            // 4A. Anatomical Leg (Thigh -> Knee -> Calf -> Ankle inside shoe collar)
            const int LEG_RINGS = 18;
            std::vector<TubeRingSample> legRings(LEG_RINGS);
            Vec3 pelvisTop = pHipTop + Vec3(0, 0.045f * H, 0);
            Vec3 ankleCuff(pAnkle.x, std::max(0.025f * H, pAnkle.y - 0.008f * H), pAnkle.z);

            for (int k = 0; k < LEG_RINGS; ++k) {
                float t = static_cast<float>(k) / (LEG_RINGS - 1);
                auto& rs = legRings[k];
                float yF = 1.0f;
                if (t < 0.50f) {
                    float a = t / 0.50f;
                    rs.center = pelvisTop * (1.0f - a) + pKnee * a;
                    yF = (prop.crotchFrac + 0.015f) + a * 0.45f * (1.0f - prop.crotchFrac - 0.015f);
                    rs.j0 = jUpLeg; rs.j1 = jLeg;
                    rs.w0 = (a < 0.78f) ? 1.0f : (1.0f - (a - 0.78f) * 2.2f);
                    rs.w1 = 1.0f - rs.w0;
                } else {
                    float a = (t - 0.50f) / 0.50f;
                    rs.center = pKnee * (1.0f - a) + ankleCuff * a;
                    yF = prop.crotchFrac + (0.46f + a * 0.48f) * (1.0f - prop.crotchFrac);
                    rs.j0 = jLeg; rs.j1 = jFoot;
                    rs.w0 = (a < 0.82f) ? 1.0f : (1.0f - (a - 0.82f) * 2.8f);
                    rs.w1 = 1.0f - rs.w0;
                }

                const auto& sl = sliceAtYFrac(yF);
                float photoLegHalfW = isLeft ? (0.5f * (sl.legLR - sl.legLL) * figAspect * H)
                                             : (0.5f * (sl.legRR - sl.legRL) * figAspect * H);
                float maxAllowed = (t < 0.35f) ? (0.056f * H) : (t < 0.65f ? 0.042f * H : 0.032f * H);
                float minAllowed = (t < 0.35f) ? (0.036f * H) : (t < 0.75f ? 0.028f * H : 0.024f * H);
                float legR = std::clamp(photoLegHalfW, minAllowed, maxAllowed);
                if (!sl.hasSplitLegs) {
                    // Skirt/dress overlap: smooth natural tapered human leg inside skirt shadow
                    legR = (0.050f - 0.022f * t + 0.005f * std::sin(t * 9.0f)) * H;
                }
                rs.rV = legR;
                rs.rU = legR * 0.96f;
                rs.vTex = std::clamp(yF, 0.05f, 0.96f);
                rs.uSpanL = isLeft ? sl.legLL : sl.legRL;
                rs.uSpanR = isLeft ? sl.legLR : sl.legRR;
            }
            AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 0, 1), legRings, 20, true);

            // 4B. Ankle Boot Collar (bridges shin neatly down into heel counter)
            float soleY = 0.001f * H;
            {
                const int CUFF_RINGS = 5;
                std::vector<TubeRingSample> cRings(CUFF_RINGS);
                for (int k = 0; k < CUFF_RINGS; ++k) {
                    float a = static_cast<float>(k) / (CUFF_RINGS - 1);
                    auto& rs = cRings[k];
                    rs.center = Vec3(pAnkle.x, (soleY + 0.018f * H) * a + (pAnkle.y + 0.045f * H) * (1.0f - a), pAnkle.z);
                    float r = (0.029f + 0.005f * a) * H;
                    rs.rV = r; rs.rU = r * 1.06f;
                    rs.j0 = (a < 0.4f) ? jLeg : jFoot;
                    rs.j1 = jFoot;
                    rs.w0 = (a < 0.4f) ? 0.6f : 1.0f; rs.w1 = 1.0f - rs.w0;
                    rs.useFixedAtlasSwatch = true;
                    rs.fixedU = atlas.shoeU;
                    rs.fixedV = atlas.shoeV;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 0, 1), cRings, 18, true);
            }

            // 4C. Grounded Flat-Soled Sculpted 3D Shoe (Heel -> Arch -> Wide Forefoot Ball -> Rounded Toe Box)
            {
                Vec3 shoeFwd((pToePos.x - pAnkle.x) * 0.45f, 0.0f, std::max(0.02f * H, pToePos.z - pAnkle.z));
                shoeFwd = (shoeFwd.LengthSq() > 1e-8f) ? shoeFwd.Normalized() : Vec3(0, 0, 1);

                struct ShoeProf { float zOff; float halfH; float halfW; float rockerY; };
                const ShoeProf stations[10] = {
                    { -0.032f * H, 0.014f * H, 0.022f * H, 0.003f * H }, // Heel back cap
                    { -0.022f * H, 0.024f * H, 0.028f * H, 0.000f * H }, // Heel counter
                    { -0.004f * H, 0.027f * H, 0.031f * H, 0.000f * H }, // Ankle/tongue
                    { +0.018f * H, 0.025f * H, 0.030f * H, 0.000f * H }, // High instep
                    { +0.042f * H, 0.020f * H, 0.031f * H, 0.000f * H }, // Mid arch
                    { +0.068f * H, 0.017f * H, 0.038f * H, 0.000f * H }, // Inner ball
                    { +0.090f * H, 0.016f * H, 0.042f * H, 0.000f * H }, // Wide forefoot ball
                    { +0.108f * H, 0.014f * H, 0.038f * H, 0.001f * H }, // Toe vamp
                    { +0.120f * H, 0.011f * H, 0.031f * H, 0.003f * H }, // Rounded toe box
                    { +0.128f * H, 0.007f * H, 0.018f * H, 0.006f * H }  // Toe tip cap
                };

                std::vector<TubeRingSample> sRings(10);
                for (int k = 0; k < 10; ++k) {
                    const auto& st = stations[k];
                    auto& rs = sRings[k];
                    float fBaseY = soleY + st.rockerY;
                    rs.center = Vec3(pAnkle.x, fBaseY + st.halfH, pAnkle.z) + shoeFwd * st.zOff;
                    rs.rU = st.halfH;
                    rs.rV = st.halfW;
                    rs.flatBottomY = fBaseY;
                    if (k <= 4) {
                        rs.j0 = jFoot; rs.j1 = jToe; rs.w0 = 1.0f; rs.w1 = 0.0f;
                    } else {
                        float a = static_cast<float>(k - 4) / 5.0f;
                        rs.j0 = jFoot; rs.j1 = jToe;
                        rs.w0 = 1.0f - a * 0.70f; rs.w1 = a * 0.70f;
                    }
                    rs.useFixedAtlasSwatch = true;
                    rs.fixedU = atlas.shoeU;
                    rs.fixedV = atlas.shoeV;
                }
                AppendClosedLoftTube(*mesh, atlas.generatedBindVerts, atlas, Vec3(0, 1, 0), sRings, 22, true);
            }
        };
        buildLegAndShoe(true,  pUpLegL, pLegL, pFootL, pToeL, sUpLegL, sLegL, sFootL, sToeL);
        buildLegAndShoe(false, pUpLegR, pLegR, pFootR, pToeR, sUpLegR, sLegR, sFootR, sToeR);

        // Attach Clean Dual-Hemisphere Texture Atlas SubMesh (matte skin/fabric shading, zero plastic glare)
        SubMesh sub;
        sub.indexOffset = 0;
        sub.indexCount = static_cast<uint32_t>(mesh->indices.size());
        sub.albedo = ColorRGB(1.0f, 1.0f, 1.0f);
        sub.metallic = 0.0f;
        sub.roughness = 0.90f;
        sub.doubleSided = false;
        sub.texWidth = atlas.texW;
        sub.texHeight = atlas.texH;
        sub.texRGBA = atlas.sharedAtlasRGBA;
        mesh->submeshes.push_back(std::move(sub));

        return mesh;
    }
};

} // namespace Surreal
