#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - SKELETAL ANIMATION & LINEAR BLEND SKINNING (LBS)
 * High-performance joint hierarchy, keyframe SLERP/LERP, and bone matrix palette
 * Designed for real-time character animation on Linux & Bare-Metal CosmOS
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_meshlet.hpp"
#if !defined(CGLTF_H_INCLUDED__)
#  if !defined(SURREAL_EXTERNAL_THIRD_PARTY_IMPL) && !defined(CGLTF_IMPLEMENTATION)
#    define CGLTF_IMPLEMENTATION
#    include "cgltf.h"
#    undef CGLTF_IMPLEMENTATION
#  else
#    include "cgltf.h"
#  endif
#endif
#if !defined(STBI_INCLUDE_STB_IMAGE_H)
#  if !defined(SURREAL_EXTERNAL_THIRD_PARTY_IMPL) && !defined(STB_IMAGE_IMPLEMENTATION)
#    define STB_IMAGE_IMPLEMENTATION
#    include "stb_image.h"
#    undef STB_IMAGE_IMPLEMENTATION
#  else
#    include "stb_image.h"
#  endif
#endif
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <map>
#include <filesystem>

namespace Surreal {

// Spherical Linear Interpolation for Quaternions
inline Quat Slerp(const Quat& q1, const Quat& q2, float t) {
    float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
    Quat qTarget = q2;

    // Take shorter path if dot < 0
    if (dot < 0.0f) {
        dot = -dot;
        qTarget.x = -qTarget.x;
        qTarget.y = -qTarget.y;
        qTarget.z = -qTarget.z;
        qTarget.w = -qTarget.w;
    }

    if (dot > 0.9995f) {
        // Linear interpolation for very close orientations
        Quat res(
            q1.x + (qTarget.x - q1.x) * t,
            q1.y + (qTarget.y - q1.y) * t,
            q1.z + (qTarget.z - q1.z) * t,
            q1.w + (qTarget.w - q1.w) * t
        );
        float len = sqrtf(res.x * res.x + res.y * res.y + res.z * res.z + res.w * res.w);
        float inv = 1.0f / (len > 0.0001f ? len : 1.0f);
        return Quat(res.x * inv, res.y * inv, res.z * inv, res.w * inv);
    }

    float theta_0 = acosf(dot);
    float theta = theta_0 * t;
    float sin_theta = sinf(theta);
    float sin_theta_0 = sinf(theta_0);

    float s0 = cosf(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;

    return Quat(
        (s0 * q1.x) + (s1 * qTarget.x),
        (s0 * q1.y) + (s1 * qTarget.y),
        (s0 * q1.z) + (s1 * qTarget.z),
        (s0 * q1.w) + (s1 * qTarget.w)
    );
}

// Linear interpolation for Vec3
inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
    return a + (b - a) * t;
}

struct SkinnedVertex3D {
    Vec3 position;
    Vec3 normal;
    float u, v;
    Vec4 color;
    uint16_t joints[4];
    float weights[4];

    SkinnedVertex3D()
        : position(), normal(0, 1.0f, 0), u(0), v(0), color(1, 1, 1, 1)
    {
        joints[0] = joints[1] = joints[2] = joints[3] = 0;
        weights[0] = 1.0f; weights[1] = weights[2] = weights[3] = 0.0f;
    }
};

struct SkeletonJoint {
    std::string name;
    int parentIndex;
    Mat4 inverseBindMatrix;
    bool hasExplicitMatrix = false;

    // Current local TRS
    Vec3 localTranslation;
    Quat localRotation;
    Vec3 localScale;

    // Base bind pose local TRS
    Vec3 baseTranslation;
    Quat baseRotation;
    Vec3 baseScale;

    // Computed world matrix & final skin matrix
    Mat4 localMatrix;
    Mat4 globalMatrix;
    Mat4 skinMatrix; // globalMatrix * inverseBindMatrix

    SkeletonJoint()
        : parentIndex(-1),
          inverseBindMatrix(Mat4::Identity()),
          hasExplicitMatrix(false),
          localTranslation(0, 0, 0),
          localRotation(0, 0, 0, 1.0f),
          localScale(1.0f, 1.0f, 1.0f),
          baseTranslation(0, 0, 0),
          baseRotation(0, 0, 0, 1.0f),
          baseScale(1.0f, 1.0f, 1.0f),
          localMatrix(Mat4::Identity()),
          globalMatrix(Mat4::Identity()),
          skinMatrix(Mat4::Identity()) {}
};

enum class ChannelTarget {
    TRANSLATION,
    ROTATION,
    SCALE
};

struct AnimationChannel {
    int jointIndex;
    ChannelTarget target;
    std::vector<float> keyTimes;
    std::vector<Vec3> vecValues;
    std::vector<Quat> quatValues;
};

struct AnimationClip {
    std::string name;
    float duration; // Total time in seconds
    std::vector<AnimationChannel> channels;

    AnimationClip() : name("DefaultClip"), duration(0.0f) {}
};

/**
 * Skeletal Character Animator & Skinning Engine
 */
class SkeletalMeshInstance {
public:
    std::vector<SkeletonJoint> joints; // All glTF scene nodes
    std::vector<int> skinJointNodeIndices; // Mapping from skin joint index to joints[]
    std::vector<Mat4> skinJointIBMs; // Inverse bind matrices per skin joint
    std::vector<Mat4> skinMatrices; // Evaluated skin matrices per skin joint
    std::vector<uint8_t> evalDone;  // Cached FK visitation flags (zero per-frame heap alloc)
    std::vector<AnimationClip> clips;
    std::vector<SkinnedVertex3D> bindVertices;
    std::shared_ptr<SurrealMesh> activeDeformedMesh;

    int activeClipIndex;
    int previousClipIndex = -1;
    float currentTime;
    float previousTime = 0.0f;
    float crossFadeDuration = 0.0f;
    float crossFadeElapsed = 0.0f;
    float playbackSpeed;
    bool isLooping;
    bool hasFootBaseOffset;
    bool staticAttributesInitialized = false;
    float footBaseOffset;
    bool isZUpModel;
    bool dirtyGPUUpload = true;
    bool skinVBOUploaded = false;
    uint64_t skinPaletteVersion = 0;
    float skinDtAccum = 0.0f;

    SkeletalMeshInstance()
        : activeDeformedMesh(std::make_shared<SurrealMesh>()),
          activeClipIndex(0),
          currentTime(0.0f),
          playbackSpeed(1.0f),
          isLooping(true),
          hasFootBaseOffset(false),
          footBaseOffset(0.0f),
          isZUpModel(false),
          skinDtAccum(0.0f) {}

    bool HasSkinData() const {
        return !bindVertices.empty() && !skinMatrices.empty() && !joints.empty();
    }

    void PlayClip(int index, bool loop = true) {
        if (index >= 0 && index < (int)clips.size()) {
            activeClipIndex = index;
            previousClipIndex = -1;
            crossFadeDuration = 0.0f;
            crossFadeElapsed = 0.0f;
            currentTime = 0.0f;
            isLooping = loop;
            skinDtAccum = 1.0f;
        }
    }

    void CrossFadeToClip(int index, float fadeDuration = 0.25f, bool loop = true) {
        if (index < 0 || index >= (int)clips.size()) return;
        if (index == activeClipIndex) {
            isLooping = loop;
            return;
        }
        if (fadeDuration <= 0.001f) {
            PlayClip(index, loop);
            return;
        }
        previousClipIndex = activeClipIndex;
        previousTime = currentTime;
        activeClipIndex = index;
        currentTime = 0.0f;
        crossFadeDuration = fadeDuration;
        crossFadeElapsed = 0.0f;
        isLooping = loop;
        skinDtAccum = 1.0f;
    }

    /**
     * Evaluate animation pose, cross-fade, FK hierarchy, and bone matrix palette only (for GPU skinning).
     * Returns true if pose was updated.
     */
    bool UpdatePoseOnly(float dt) {
        if (joints.empty()) return false;

        // Advance time even when bindVertices is empty? No, static clip-less check:
        const bool hasActiveAnim = (activeClipIndex >= 0 && activeClipIndex < (int)clips.size() &&
                                    clips[activeClipIndex].duration > 0.0f && !clips[activeClipIndex].channels.empty());
        if (!hasActiveAnim && hasFootBaseOffset && staticAttributesInitialized) {
            return false;
        }

        EvaluatePoseAndPalette(dt);
        EnsureFootBaseOffsetInitialized();
        skinPaletteVersion++;
        return true;
    }

    /**
     * Advance animation time, sample keyframe curves, update hierarchy, and deform mesh (CPU LBS fallback).
     */
    void Update(float dt, bool cpuDeform = true) {
        if (joints.empty() || bindVertices.empty()) return;

        const bool hasActiveAnim = (activeClipIndex >= 0 && activeClipIndex < (int)clips.size() &&
                                    clips[activeClipIndex].duration > 0.0f && !clips[activeClipIndex].channels.empty());
        if (!hasActiveAnim && hasFootBaseOffset && staticAttributesInitialized) {
            return;
        }

        if (!cpuDeform) {
            UpdatePoseOnly(dt);
            return;
        }

        // High-poly avatar cadence limiter only for heavy CPU LBS path (GPU path runs at full framerate!)
        if (hasFootBaseOffset && staticAttributesInitialized && bindVertices.size() > 25000 && dt > 0.0f && dt < 0.032f) {
            skinDtAccum += dt;
            if (skinDtAccum < 0.032f) {
                return;
            }
            dt = skinDtAccum;
            skinDtAccum = 0.0f;
        } else {
            skinDtAccum = 0.0f;
        }

        EvaluatePoseAndPalette(dt);
        skinPaletteVersion++;
        DeformMesh();
    }

private:
    static void AdvanceClipClock(const AnimationClip& clip, float dt, float speed, bool loop, float& inOutTime) {
        if (clip.duration <= 0.0f) return;
        inOutTime += dt * speed;
        if (inOutTime > clip.duration) {
            inOutTime = loop ? fmodf(inOutTime, clip.duration) : clip.duration;
        } else if (inOutTime < 0.0f) {
            inOutTime = 0.0f;
        }
    }

    void SampleClipIntoPose(const AnimationClip& clip, float sampleTime,
                            std::vector<Vec3>& tr, std::vector<Quat>& rot, std::vector<Vec3>& scl,
                            std::vector<uint8_t>& explicitFlag) const {
        for (const auto& chan : clip.channels) {
            if (chan.jointIndex < 0 || chan.jointIndex >= (int)joints.size()) continue;
            if (chan.keyTimes.empty()) continue;

            if (chan.keyTimes.size() == 1) {
                if (chan.target == ChannelTarget::TRANSLATION && !chan.vecValues.empty()) {
                    tr[chan.jointIndex] = chan.vecValues[0];
                    explicitFlag[chan.jointIndex] = 0;
                } else if (chan.target == ChannelTarget::ROTATION && !chan.quatValues.empty()) {
                    rot[chan.jointIndex] = chan.quatValues[0];
                    explicitFlag[chan.jointIndex] = 0;
                } else if (chan.target == ChannelTarget::SCALE && !chan.vecValues.empty()) {
                    scl[chan.jointIndex] = chan.vecValues[0];
                    explicitFlag[chan.jointIndex] = 0;
                }
                continue;
            }

            auto itUpper = std::upper_bound(chan.keyTimes.begin(), chan.keyTimes.end(), sampleTime);
            size_t k = 0;
            if (itUpper == chan.keyTimes.begin()) k = 0;
            else if (itUpper == chan.keyTimes.end()) k = chan.keyTimes.size() - 2;
            else k = static_cast<size_t>(std::distance(chan.keyTimes.begin(), itUpper)) - 1;

            float t0 = chan.keyTimes[k];
            float t1 = chan.keyTimes[k + 1];
            float factor = (t1 > t0) ? std::clamp((sampleTime - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;

            if (chan.target == ChannelTarget::TRANSLATION && k + 1 < chan.vecValues.size()) {
                tr[chan.jointIndex] = Lerp(chan.vecValues[k], chan.vecValues[k + 1], factor);
                explicitFlag[chan.jointIndex] = 0;
            } else if (chan.target == ChannelTarget::ROTATION && k + 1 < chan.quatValues.size()) {
                rot[chan.jointIndex] = Slerp(chan.quatValues[k], chan.quatValues[k + 1], factor);
                explicitFlag[chan.jointIndex] = 0;
            } else if (chan.target == ChannelTarget::SCALE && k + 1 < chan.vecValues.size()) {
                scl[chan.jointIndex] = Lerp(chan.vecValues[k], chan.vecValues[k + 1], factor);
                explicitFlag[chan.jointIndex] = 0;
            }
        }
    }

    void EvaluatePoseAndPalette(float dt) {
        const size_t jc = joints.size();
        std::vector<Vec3> curTr(jc), prevTr(jc);
        std::vector<Quat> curRot(jc), prevRot(jc);
        std::vector<Vec3> curScl(jc), prevScl(jc);
        std::vector<uint8_t> curExpl(jc), prevExpl(jc);

        for (size_t i = 0; i < jc; ++i) {
            curTr[i] = prevTr[i] = joints[i].baseTranslation;
            curRot[i] = prevRot[i] = joints[i].baseRotation;
            curScl[i] = prevScl[i] = joints[i].baseScale;
            curExpl[i] = prevExpl[i] = joints[i].hasExplicitMatrix ? 1 : 0;
        }

        if (activeClipIndex >= 0 && activeClipIndex < (int)clips.size()) {
            const AnimationClip& curClip = clips[activeClipIndex];
            AdvanceClipClock(curClip, dt, playbackSpeed, isLooping, currentTime);
            SampleClipIntoPose(curClip, currentTime, curTr, curRot, curScl, curExpl);
        }

        bool blending = (previousClipIndex >= 0 && previousClipIndex < (int)clips.size() && crossFadeDuration > 0.001f);
        if (blending) {
            crossFadeElapsed += std::max(0.0f, dt);
            if (crossFadeElapsed >= crossFadeDuration) {
                previousClipIndex = -1;
                crossFadeDuration = 0.0f;
                crossFadeElapsed = 0.0f;
                blending = false;
            } else {
                const AnimationClip& prevClip = clips[previousClipIndex];
                AdvanceClipClock(prevClip, dt, playbackSpeed, true, previousTime);
                SampleClipIntoPose(prevClip, previousTime, prevTr, prevRot, prevScl, prevExpl);
                float u = std::clamp(crossFadeElapsed / crossFadeDuration, 0.0f, 1.0f);
                float blend = u * u * (3.0f - 2.0f * u); // Smoothstep crossfade
                for (size_t i = 0; i < jc; ++i) {
                    joints[i].localTranslation = Lerp(prevTr[i], curTr[i], blend);
                    joints[i].localRotation = Slerp(prevRot[i], curRot[i], blend);
                    joints[i].localScale = Lerp(prevScl[i], curScl[i], blend);
                    joints[i].hasExplicitMatrix = (curExpl[i] != 0 && prevExpl[i] != 0);
                }
            }
        }

        if (!blending) {
            for (size_t i = 0; i < jc; ++i) {
                joints[i].localTranslation = curTr[i];
                joints[i].localRotation = curRot[i];
                joints[i].localScale = curScl[i];
                joints[i].hasExplicitMatrix = (curExpl[i] != 0);
            }
        }

        for (auto& j : joints) {
            if (!j.hasExplicitMatrix) {
                j.localMatrix = Mat4::Translation(j.localTranslation) *
                                Mat4::FromQuat(j.localRotation) *
                                Mat4::Scaling(j.localScale);
            }
        }

        if (evalDone.size() != jc) {
            evalDone.resize(jc, 0);
        } else {
            std::fill(evalDone.begin(), evalDone.end(), 0);
        }
        auto evalGlobal = [&](auto& self, int idx) -> void {
            if (evalDone[idx]) return;
            SkeletonJoint& j = joints[idx];
            if (j.parentIndex >= 0 && j.parentIndex < (int)jc) {
                self(self, j.parentIndex);
                j.globalMatrix = joints[j.parentIndex].globalMatrix * j.localMatrix;
            } else {
                j.globalMatrix = j.localMatrix;
            }
            evalDone[idx] = 1;
        };
        for (size_t i = 0; i < jc; ++i) {
            evalGlobal(evalGlobal, static_cast<int>(i));
        }

        if (!skinJointNodeIndices.empty()) {
            skinMatrices.resize(skinJointNodeIndices.size());
            for (size_t s = 0; s < skinJointNodeIndices.size(); ++s) {
                int nodeIdx = skinJointNodeIndices[s];
                if (nodeIdx >= 0 && nodeIdx < (int)jc) {
                    skinMatrices[s] = joints[nodeIdx].globalMatrix * skinJointIBMs[s];
                    joints[nodeIdx].skinMatrix = skinMatrices[s];
                }
            }
        } else {
            skinMatrices.resize(jc);
            for (size_t i = 0; i < jc; ++i) {
                joints[i].skinMatrix = joints[i].globalMatrix * joints[i].inverseBindMatrix;
                skinMatrices[i] = joints[i].skinMatrix;
            }
        }
    }

    void EnsureFootBaseOffsetInitialized() {
        if (!activeDeformedMesh || bindVertices.empty()) return;
        const size_t vertCount = bindVertices.size();
        bool needsBoundsInit = false;
        if (activeDeformedMesh->vertices.size() != vertCount || !staticAttributesInitialized) {
            activeDeformedMesh->vertices.resize(vertCount);
            for (size_t i = 0; i < vertCount; ++i) {
                activeDeformedMesh->vertices[i].position = bindVertices[i].position;
                activeDeformedMesh->vertices[i].normal = bindVertices[i].normal;
                activeDeformedMesh->vertices[i].u = bindVertices[i].u;
                activeDeformedMesh->vertices[i].v = bindVertices[i].v;
                activeDeformedMesh->vertices[i].color = bindVertices[i].color;
            }
            staticAttributesInitialized = true;
            needsBoundsInit = true;
        }
        const size_t numSkinMatrices = skinMatrices.size();
        if (!hasFootBaseOffset && numSkinMatrices > 0) {
            float minGroundY = 1e9f;
            for (size_t i = 0; i < vertCount; ++i) {
                const SkinnedVertex3D& b = bindVertices[i];
                float sy = 0.0f;
                for (int w = 0; w < 4; ++w) {
                    float weight = b.weights[w];
                    if (weight > 0.0001f && b.joints[w] < numSkinMatrices) {
                        const Mat4& sm = skinMatrices[b.joints[w]];
                        sy += (sm.m[1][0] * b.position.x + sm.m[1][1] * b.position.y + sm.m[1][2] * b.position.z + sm.m[1][3]) * weight;
                    }
                }
                if (sy < minGroundY) minGroundY = sy;
            }
            footBaseOffset = std::isfinite(minGroundY) ? minGroundY : 0.0f;
            hasFootBaseOffset = true;
            needsBoundsInit = true;
        }
        if (needsBoundsInit) {
            activeDeformedMesh->CalculateBounds();
        }
    }

    void DeformMesh() {
        if (!activeDeformedMesh) return;
        EnsureFootBaseOffsetInitialized();
        const size_t vertCount = bindVertices.size();
        const size_t numSkinMatrices = skinMatrices.size();
        if (numSkinMatrices == 0) return;

        const float footOffset = footBaseOffset;
        const Mat4* __restrict skinMatPtr = skinMatrices.data();
        const SkinnedVertex3D* __restrict bindPtr = bindVertices.data();
        Vertex3D* __restrict outPtr = activeDeformedMesh->vertices.data();

        // Multi-threaded Pre-Blended 3x4 Affine Linear Blend Skinning (LBS) via OpenMP
        // Avoid thread-barrier wakeups for compact (<8192 vertex) auto-rigged dancers
        #pragma omp parallel for schedule(static) if(vertCount >= 8192)
        for (int i = 0; i < (int)vertCount; ++i) {
            const SkinnedVertex3D& b = bindPtr[i];
            const float px = b.position.x, py = b.position.y, pz = b.position.z;
            const float nx = b.normal.x,   ny = b.normal.y,   nz = b.normal.z;
            Vertex3D& dst = outPtr[i];

            // Fast single-bone path (avoids 4-matrix blend & inverse-sqrt normalization)
            if (b.weights[0] > 0.999f && b.joints[0] < numSkinMatrices) {
                const Mat4& sm = skinMatPtr[b.joints[0]];
                dst.position.x = sm.m[0][0] * px + sm.m[0][1] * py + sm.m[0][2] * pz + sm.m[0][3];
                dst.position.y = (sm.m[1][0] * px + sm.m[1][1] * py + sm.m[1][2] * pz + sm.m[1][3]) - footOffset;
                dst.position.z = sm.m[2][0] * px + sm.m[2][1] * py + sm.m[2][2] * pz + sm.m[2][3];
                dst.normal.x = sm.m[0][0] * nx + sm.m[0][1] * ny + sm.m[0][2] * nz;
                dst.normal.y = sm.m[1][0] * nx + sm.m[1][1] * ny + sm.m[1][2] * nz;
                dst.normal.z = sm.m[2][0] * nx + sm.m[2][1] * ny + sm.m[2][2] * nz;
                continue;
            }

            float m00 = 0.0f, m01 = 0.0f, m02 = 0.0f, m03 = 0.0f;
            float m10 = 0.0f, m11 = 0.0f, m12 = 0.0f, m13 = 0.0f;
            float m20 = 0.0f, m21 = 0.0f, m22 = 0.0f, m23 = 0.0f;

            for (int w = 0; w < 4; ++w) {
                const float weight = b.weights[w];
                if (weight > 0.0001f) {
                    const uint16_t jIdx = b.joints[w];
                    if (jIdx < numSkinMatrices) {
                        const Mat4& sm = skinMatPtr[jIdx];
                        m00 += sm.m[0][0] * weight; m01 += sm.m[0][1] * weight; m02 += sm.m[0][2] * weight; m03 += sm.m[0][3] * weight;
                        m10 += sm.m[1][0] * weight; m11 += sm.m[1][1] * weight; m12 += sm.m[1][2] * weight; m13 += sm.m[1][3] * weight;
                        m20 += sm.m[2][0] * weight; m21 += sm.m[2][1] * weight; m22 += sm.m[2][2] * weight; m23 += sm.m[2][3] * weight;
                    }
                }
            }

            dst.position.x = m00 * px + m01 * py + m02 * pz + m03;
            dst.position.y = (m10 * px + m11 * py + m12 * pz + m13) - footOffset;
            dst.position.z = m20 * px + m21 * py + m22 * pz + m23;

            const float rnx = m00 * nx + m01 * ny + m02 * nz;
            const float rny = m10 * nx + m11 * ny + m12 * nz;
            const float rnz = m20 * nx + m21 * ny + m22 * nz;
            const float lenSq = rnx * rnx + rny * rny + rnz * rnz;
            const float invLen = (lenSq > 1e-12f) ? (1.0f / std::sqrt(lenSq)) : 1.0f;
            dst.normal.x = rnx * invLen;
            dst.normal.y = rny * invLen;
            dst.normal.z = rnz * invLen;
        }

        dirtyGPUUpload = true;
    }
};

/**
 * glTF 2.0 Skeletal Animation Importer
 */
class SurrealSkeletalLoader {
public:
    static bool LoadSkeletalModel(
        const std::string& filepath,
        SkeletalMeshInstance& outInstance)
    {
        std::error_code ec;
        std::string resolvedPath = filepath;
        if (std::filesystem::is_symlink(filepath, ec)) {
            auto can = std::filesystem::canonical(filepath, ec);
            if (!ec) resolvedPath = can.string();
        }

        cgltf_options options;
        memset(&options, 0, sizeof(cgltf_options));

        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, resolvedPath.c_str(), &data);
        if (result != cgltf_result_success) return false;

        result = cgltf_load_buffers(&options, data, resolvedPath.c_str());
        if (result != cgltf_result_success) {
            cgltf_free(data);
            return false;
        }

        // 1. Parse All glTF Scene Graph Nodes into Joint Rig
        outInstance.joints.resize(data->nodes_count);
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            cgltf_node* cn = &data->nodes[i];
            SkeletonJoint& joint = outInstance.joints[i];
            joint.name = cn->name ? cn->name : ("Node_" + std::to_string(i));
            joint.parentIndex = cn->parent ? static_cast<int>(cn->parent - data->nodes) : -1;

            if (cn->has_matrix) {
                float lm[16];
                cgltf_node_transform_local(cn, lm);
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        joint.localMatrix.m[r][c] = lm[c * 4 + r];
                    }
                }
                joint.hasExplicitMatrix = true;
            } else {
                if (cn->has_translation) joint.baseTranslation = Vec3(cn->translation[0], cn->translation[1], cn->translation[2]);
                if (cn->has_rotation) joint.baseRotation = Quat(cn->rotation[0], cn->rotation[1], cn->rotation[2], cn->rotation[3]);
                if (cn->has_scale) joint.baseScale = Vec3(cn->scale[0], cn->scale[1], cn->scale[2]);
                joint.localTranslation = joint.baseTranslation;
                joint.localRotation = joint.baseRotation;
                joint.localScale = joint.baseScale;
                joint.localMatrix = Mat4::Translation(joint.localTranslation) *
                                    Mat4::FromQuat(joint.localRotation) *
                                    Mat4::Scaling(joint.localScale);
                joint.hasExplicitMatrix = false;
            }
        }

        // 2. Parse Multi-Skin Joint Mapping & Inverse Bind Matrices
        std::vector<size_t> skinOffsets(data->skins_count, 0);
        if (data->skins_count > 0) {
            for (cgltf_size s = 0; s < data->skins_count; ++s) {
                skinOffsets[s] = outInstance.skinJointNodeIndices.size();
                const cgltf_skin& skin = data->skins[s];
                for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                    outInstance.skinJointNodeIndices.push_back(static_cast<int>(skin.joints[j] - data->nodes));
                    Mat4 ibm = Mat4::Identity();
                    if (skin.inverse_bind_matrices) {
                        float ibmRaw[16];
                        cgltf_accessor_read_float(skin.inverse_bind_matrices, j, ibmRaw, 16);
                        for (int r = 0; r < 4; ++r) {
                            for (int c = 0; c < 4; ++c) {
                                ibm.m[r][c] = ibmRaw[c * 4 + r];
                            }
                        }
                    }
                    outInstance.skinJointIBMs.push_back(ibm);
                }
            }
        }

        // 3. Parse Animation Clips & Channels (every channel connects directly to scene node)
        for (cgltf_size a = 0; a < data->animations_count; ++a) {
            const cgltf_animation& gAnim = data->animations[a];
            AnimationClip clip;
            clip.name = gAnim.name ? gAnim.name : ("Clip_" + std::to_string(a));
            clip.duration = 0.0f;

            for (cgltf_size c = 0; c < gAnim.channels_count; ++c) {
                const cgltf_animation_channel& gChan = gAnim.channels[c];
                cgltf_node* targetNode = gChan.target_node;
                if (!targetNode) continue;
                int nodeIdx = static_cast<int>(targetNode - data->nodes);
                if (nodeIdx < 0 || nodeIdx >= (int)outInstance.joints.size()) continue;

                AnimationChannel chan;
                chan.jointIndex = nodeIdx;

                if (gChan.target_path == cgltf_animation_path_type_translation) {
                    chan.target = ChannelTarget::TRANSLATION;
                } else if (gChan.target_path == cgltf_animation_path_type_rotation) {
                    chan.target = ChannelTarget::ROTATION;
                } else if (gChan.target_path == cgltf_animation_path_type_scale) {
                    chan.target = ChannelTarget::SCALE;
                } else {
                    continue;
                }

                cgltf_animation_sampler* sampler = gChan.sampler;
                cgltf_accessor* timeAcc = sampler->input;
                cgltf_accessor* valAcc = sampler->output;

                chan.keyTimes.resize(timeAcc->count);
                for (cgltf_size k = 0; k < timeAcc->count; ++k) {
                    cgltf_accessor_read_float(timeAcc, k, &chan.keyTimes[k], 1);
                    if (chan.keyTimes[k] > clip.duration) {
                        clip.duration = chan.keyTimes[k];
                    }
                }

                if (chan.target == ChannelTarget::ROTATION) {
                    chan.quatValues.resize(valAcc->count);
                    for (cgltf_size k = 0; k < valAcc->count; ++k) {
                        float q[4];
                        cgltf_accessor_read_float(valAcc, k, q, 4);
                        chan.quatValues[k] = Quat(q[0], q[1], q[2], q[3]);
                    }
                } else {
                    chan.vecValues.resize(valAcc->count);
                    for (cgltf_size k = 0; k < valAcc->count; ++k) {
                        float v[3];
                        cgltf_accessor_read_float(valAcc, k, v, 3);
                        chan.vecValues[k] = Vec3(v[0], v[1], v[2]);
                    }
                }

                clip.channels.push_back(chan);
            }

            outInstance.clips.push_back(clip);
        }

        // 3. Texture Cache & Loader (storing RGBA buffer in SubMesh)
        size_t lastSlash = resolvedPath.find_last_of("/\\");
        std::string baseDir = (lastSlash != std::string::npos) ? resolvedPath.substr(0, lastSlash) : ".";

        struct RawTex {
            int w = 0, h = 0;
            std::shared_ptr<std::vector<uint8_t>> rgba;
        };
        std::map<const void*, RawTex> texCache;

        auto getTex = [&](cgltf_image* img) -> RawTex {
            RawTex res;
            if (!img) return res;
            const void* key = img->buffer_view ? (const void*)img->buffer_view : (const void*)img->uri;
            if (texCache.count(key)) return texCache[key];

            int w = 0, h = 0, ch = 0;
            unsigned char* pix = nullptr;
            if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
                const uint8_t* ptr = (const uint8_t*)img->buffer_view->buffer->data + img->buffer_view->offset;
                pix = stbi_load_from_memory(ptr, (int)img->buffer_view->size, &w, &h, &ch, 4);
            } else if (img->uri) {
                std::string full = baseDir + "/" + img->uri;
                pix = stbi_load(full.c_str(), &w, &h, &ch, 4);
            }
            if (pix) {
                res.w = w;
                res.h = h;
                res.rgba = std::make_shared<std::vector<uint8_t>>(pix, pix + (w * h * 4));
                stbi_image_free(pix);
                texCache[key] = res;
            }
            return res;
        };

        // 4. Static / Scene Graph Models without Skins (Seoul City, Environments, Props)
        if (data->skins_count == 0) {
            outInstance.activeDeformedMesh->vertices.clear();
            outInstance.activeDeformedMesh->indices.clear();
            outInstance.activeDeformedMesh->submeshes.clear();

            for (cgltf_size i = 0; i < data->nodes_count; ++i) {
                cgltf_node* node = &data->nodes[i];
                if (!node->mesh) continue;

                float m[16];
                cgltf_node_transform_world(node, m);
                Mat4 nodeWorld = Mat4::Identity();
                for (int r = 0; r < 4; ++r) {
                    for (int c = 0; c < 4; ++c) {
                        nodeWorld.m[r][c] = m[c * 4 + r];
                    }
                }
                Mat4 nodeNormalMat = nodeWorld.InverseTranspose3x3();

                cgltf_mesh* gMesh = node->mesh;
                for (cgltf_size p = 0; p < gMesh->primitives_count; ++p) {
                    const cgltf_primitive& prim = gMesh->primitives[p];
                    if (prim.type != cgltf_primitive_type_triangles) continue;

                    const cgltf_accessor* posAcc = nullptr;
                    const cgltf_accessor* normAcc = nullptr;
                    const cgltf_accessor* uvAcc = nullptr;

                    for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                        if (prim.attributes[a].type == cgltf_attribute_type_position) posAcc = prim.attributes[a].data;
                        else if (prim.attributes[a].type == cgltf_attribute_type_normal) normAcc = prim.attributes[a].data;
                        else if (prim.attributes[a].type == cgltf_attribute_type_texcoord && prim.attributes[a].index == 0) uvAcc = prim.attributes[a].data;
                    }
                    if (!posAcc) continue;

                    // Skip invisible volumetric helper / fog meshes (e.g. fog_88 with alpha=0)
                    if (prim.material) {
                        if (prim.material->has_pbr_metallic_roughness) {
                            const auto& pbr = prim.material->pbr_metallic_roughness;
                            if (pbr.base_color_factor[3] < 0.05f && (!pbr.base_color_texture.texture || !pbr.base_color_texture.texture->image)) {
                                continue; // Skip invisible collision or fog mesh
                            }
                        } else if (prim.material->has_pbr_specular_glossiness) {
                            const auto& sg = prim.material->pbr_specular_glossiness;
                            if (sg.diffuse_factor[3] < 0.05f && (!sg.diffuse_texture.texture || !sg.diffuse_texture.texture->image)) {
                                continue;
                            }
                        }
                    }
                    if (node->name && std::string(node->name).find("fog") != std::string::npos) {
                        continue;
                    }

                    SubMesh sub;
                    sub.indexOffset = static_cast<uint32_t>(outInstance.activeDeformedMesh->indices.size());

                    bool hasUvTransform = false;
                    float uvScale[2] = {1.0f, 1.0f};
                    float uvOffset[2] = {0.0f, 0.0f};

                    if (prim.material) {
                        sub.emissive = ColorRGB(
                            prim.material->emissive_factor[0],
                            prim.material->emissive_factor[1],
                            prim.material->emissive_factor[2]
                        );
                        if (prim.material->has_emissive_strength) {
                            sub.emissiveStrength = prim.material->emissive_strength.emissive_strength;
                        }
                        sub.doubleSided = (prim.material->double_sided != 0);
                        sub.alphaCutoff = prim.material->alpha_cutoff;
                        if (prim.material->alpha_mode == cgltf_alpha_mode_mask) sub.alphaMode = PBRMaterial::AlphaMode::MASK_MODE;
                        else if (prim.material->alpha_mode == cgltf_alpha_mode_blend) sub.alphaMode = PBRMaterial::AlphaMode::BLEND_MODE;

                        if (prim.material->normal_texture.texture && prim.material->normal_texture.texture->image) {
                            RawTex rt = getTex(prim.material->normal_texture.texture->image);
                            sub.normalTexW = rt.w; sub.normalTexH = rt.h; sub.normalTexRGBA = rt.rgba;
                        }
                        if (prim.material->occlusion_texture.texture && prim.material->occlusion_texture.texture->image) {
                            RawTex rt = getTex(prim.material->occlusion_texture.texture->image);
                            sub.aoTexW = rt.w; sub.aoTexH = rt.h; sub.aoTexRGBA = rt.rgba;
                        }
                        if (prim.material->emissive_texture.texture && prim.material->emissive_texture.texture->image) {
                            RawTex rt = getTex(prim.material->emissive_texture.texture->image);
                            sub.emissiveTexW = rt.w; sub.emissiveTexH = rt.h; sub.emissiveTexRGBA = rt.rgba;
                        }

                        if (prim.material->has_pbr_metallic_roughness) {
                            const auto& pbr = prim.material->pbr_metallic_roughness;
                            sub.albedo = ColorRGB(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
                            sub.metallic = pbr.metallic_factor;
                            sub.roughness = pbr.roughness_factor;

                            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                                RawTex rt = getTex(pbr.base_color_texture.texture->image);
                                sub.texWidth = rt.w;
                                sub.texHeight = rt.h;
                                sub.texRGBA = rt.rgba;

                                if (pbr.base_color_texture.has_transform) {
                                    hasUvTransform = true;
                                    uvScale[0] = pbr.base_color_texture.transform.scale[0];
                                    uvScale[1] = pbr.base_color_texture.transform.scale[1];
                                    uvOffset[0] = pbr.base_color_texture.transform.offset[0];
                                    uvOffset[1] = pbr.base_color_texture.transform.offset[1];
                                }
                            }
                            if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image) {
                                RawTex rt = getTex(pbr.metallic_roughness_texture.texture->image);
                                sub.mrTexW = rt.w; sub.mrTexH = rt.h; sub.mrTexRGBA = rt.rgba;
                            }
                        } else if (prim.material->has_pbr_specular_glossiness) {
                            const auto& sg = prim.material->pbr_specular_glossiness;
                            sub.albedo = ColorRGB(sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2]);
                            sub.metallic = 0.08f;
                            sub.roughness = std::max(0.15f, 1.0f - sg.glossiness_factor);

                            if (sg.diffuse_texture.texture && sg.diffuse_texture.texture->image) {
                                RawTex rt = getTex(sg.diffuse_texture.texture->image);
                                sub.texWidth = rt.w;
                                sub.texHeight = rt.h;
                                sub.texRGBA = rt.rgba;

                                if (sg.diffuse_texture.has_transform) {
                                    hasUvTransform = true;
                                    uvScale[0] = sg.diffuse_texture.transform.scale[0];
                                    uvScale[1] = sg.diffuse_texture.transform.scale[1];
                                    uvOffset[0] = sg.diffuse_texture.transform.offset[0];
                                    uvOffset[1] = sg.diffuse_texture.transform.offset[1];
                                }
                            }
                        }

                        // Calibrate glowing elements: shop windows, neon signs, and street lamps
                        if (prim.material->name) {
                            std::string matName = prim.material->name;
                            for (char& c : matName) c = tolower(c);

                            if (matName.find("windows2") != std::string::npos ||
                                matName.find("windows3") != std::string::npos ||
                                matName.find("wiindos") != std::string::npos) {
                                sub.emissive = ColorRGB(1.8f, 1.55f, 1.15f); // Warm interior room/shop lighting
                                sub.emissiveStrength = 2.0f;
                            } else if (matName.find("material.025") != std::string::npos ||
                                       matName.find("lamp") != std::string::npos ||
                                       matName.find("lantern") != std::string::npos ||
                                       matName.find("light") != std::string::npos) {
                                sub.emissive = ColorRGB(5.5f, 4.2f, 2.2f); // Intense street lamp bulb glow
                                sub.emissiveStrength = 4.0f;
                            } else if (matName.find("material.035") != std::string::npos ||
                                       matName.find("material.026") != std::string::npos ||
                                       matName.find("sign") != std::string::npos ||
                                       matName.find("billboard") != std::string::npos ||
                                       matName.find("neon") != std::string::npos) {
                                sub.emissive = ColorRGB(3.0f, 2.6f, 2.2f); // Radiant storefront signage & billboard
                                sub.emissiveStrength = 2.5f;
                            }
                        }
                    }

                    // If no texture is present, calibrate architectural facade tones instead of flat white
                    if (!sub.texRGBA && prim.material && prim.material->name) {
                        std::string matName = prim.material->name;
                        for (char& c : matName) c = tolower(c);
                        if (matName.find("wall") != std::string::npos) {
                            sub.albedo = ColorRGB(0.62f, 0.65f, 0.68f); // Urban concrete facade
                            sub.roughness = 0.82f;
                        } else if (matName.find("tiles2") != std::string::npos) {
                            sub.albedo = ColorRGB(0.38f, 0.40f, 0.44f); // Dark trim tiles
                            sub.roughness = 0.55f;
                        } else if (matName.find("tiles") != std::string::npos) {
                            sub.albedo = ColorRGB(0.70f, 0.72f, 0.75f); // Ceramic building tiles
                            sub.roughness = 0.60f;
                        } else if (matName.find("roof") != std::string::npos) {
                            sub.albedo = ColorRGB(0.18f, 0.20f, 0.23f); // Rooftop tar/gravel membrane
                            sub.roughness = 0.90f;
                        } else if (matName.find("hous") != std::string::npos) {
                            sub.albedo = ColorRGB(0.58f, 0.60f, 0.64f); // Facade concrete
                            sub.roughness = 0.75f;
                        }
                    }

                    uint32_t vertBase = static_cast<uint32_t>(outInstance.activeDeformedMesh->vertices.size());
                    outInstance.activeDeformedMesh->vertices.resize(vertBase + posAcc->count);

                    for (cgltf_size v = 0; v < posAcc->count; ++v) {
                        Vertex3D& vert = outInstance.activeDeformedMesh->vertices[vertBase + v];

                        float lp[3];
                        cgltf_accessor_read_float(posAcc, v, lp, 3);
                        vert.position.x = m[0]*lp[0] + m[4]*lp[1] + m[8]*lp[2]  + m[12];
                        vert.position.y = m[1]*lp[0] + m[5]*lp[1] + m[9]*lp[2]  + m[13];
                        vert.position.z = m[2]*lp[0] + m[6]*lp[1] + m[10]*lp[2] + m[14];

                        if (normAcc) {
                            float ln[3];
                            cgltf_accessor_read_float(normAcc, v, ln, 3);
                            vert.normal = nodeNormalMat.TransformDirection(Vec3(ln[0], ln[1], ln[2])).Normalized();
                        } else {
                            vert.normal = Vec3(0, 1.0f, 0);
                        }

                        if (uvAcc) {
                            float uv[2];
                            cgltf_accessor_read_float(uvAcc, v, uv, 2);
                            if (hasUvTransform) {
                                vert.u = uv[0] * uvScale[0] + uvOffset[0];
                                vert.v = uv[1] * uvScale[1] + uvOffset[1];
                            } else {
                                vert.u = uv[0];
                                vert.v = uv[1];
                            }
                        }
                        vert.color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }

                    if (prim.indices) {
                        sub.indexCount = static_cast<uint32_t>(prim.indices->count);
                        outInstance.activeDeformedMesh->indices.reserve(outInstance.activeDeformedMesh->indices.size() + sub.indexCount);
                        for (cgltf_size k = 0; k < prim.indices->count; ++k) {
                            uint32_t idx = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, k));
                            outInstance.activeDeformedMesh->indices.push_back(vertBase + idx);
                        }
                    } else {
                        sub.indexCount = static_cast<uint32_t>(posAcc->count);
                        for (uint32_t k = 0; k < sub.indexCount; ++k) {
                            outInstance.activeDeformedMesh->indices.push_back(vertBase + k);
                        }
                    }

                    outInstance.activeDeformedMesh->submeshes.push_back(sub);
                }
            }

            // Batch and coalesce static submeshes that share identical texture & material parameters
            // Reduces OpenGL draw calls on multi-node city environments (e.g. Seoul_City_PBR.glb) by ~10x!
            if (outInstance.activeDeformedMesh->submeshes.size() > 1) {
                auto& subs = outInstance.activeDeformedMesh->submeshes;
                const auto& oldIndices = outInstance.activeDeformedMesh->indices;
                std::vector<size_t> order(subs.size());
                for (size_t i = 0; i < subs.size(); ++i) order[i] = i;

                auto sameMat = [&](const SubMesh& a, const SubMesh& b) -> bool {
                    return a.texRGBA.get() == b.texRGBA.get() &&
                           a.normalTexRGBA.get() == b.normalTexRGBA.get() &&
                           a.mrTexRGBA.get() == b.mrTexRGBA.get() &&
                           a.aoTexRGBA.get() == b.aoTexRGBA.get() &&
                           a.emissiveTexRGBA.get() == b.emissiveTexRGBA.get() &&
                           a.alphaMode == b.alphaMode &&
                           a.doubleSided == b.doubleSided &&
                           std::abs(a.albedo.r - b.albedo.r) < 0.01f &&
                           std::abs(a.albedo.g - b.albedo.g) < 0.01f &&
                           std::abs(a.albedo.b - b.albedo.b) < 0.01f &&
                           std::abs(a.emissive.r - b.emissive.r) < 0.01f &&
                           std::abs(a.emissive.g - b.emissive.g) < 0.01f &&
                           std::abs(a.emissive.b - b.emissive.b) < 0.01f &&
                           std::abs(a.roughness - b.roughness) < 0.02f &&
                           std::abs(a.metallic - b.metallic) < 0.02f;
                };

                std::sort(order.begin(), order.end(), [&](size_t ia, size_t ib) {
                    const SubMesh& a = subs[ia];
                    const SubMesh& b = subs[ib];
                    if (a.texRGBA.get() != b.texRGBA.get()) return a.texRGBA.get() < b.texRGBA.get();
                    if (a.normalTexRGBA.get() != b.normalTexRGBA.get()) return a.normalTexRGBA.get() < b.normalTexRGBA.get();
                    if (a.mrTexRGBA.get() != b.mrTexRGBA.get()) return a.mrTexRGBA.get() < b.mrTexRGBA.get();
                    if (a.alphaMode != b.alphaMode) return static_cast<int>(a.alphaMode) < static_cast<int>(b.alphaMode);
                    if (std::abs(a.emissive.r - b.emissive.r) >= 0.01f) return a.emissive.r < b.emissive.r;
                    if (std::abs(a.albedo.r - b.albedo.r) >= 0.01f) return a.albedo.r < b.albedo.r;
                    return ia < ib;
                });

                std::vector<uint32_t> mergedIndices;
                mergedIndices.reserve(oldIndices.size());
                std::vector<SubMesh> mergedSubs;

                for (size_t idx : order) {
                    const SubMesh& s = subs[idx];
                    if (s.indexCount == 0) continue;
                    if (!mergedSubs.empty() && sameMat(mergedSubs.back(), s)) {
                        mergedSubs.back().indexCount += s.indexCount;
                    } else {
                        SubMesh ns = s;
                        ns.indexOffset = static_cast<uint32_t>(mergedIndices.size());
                        ns.indexCount = s.indexCount;
                        mergedSubs.push_back(std::move(ns));
                    }
                    auto beg = oldIndices.begin() + s.indexOffset;
                    mergedIndices.insert(mergedIndices.end(), beg, beg + s.indexCount);
                }

                outInstance.activeDeformedMesh->indices = std::move(mergedIndices);
                outInstance.activeDeformedMesh->submeshes = std::move(mergedSubs);
            }

            outInstance.activeDeformedMesh->CalculateBounds();

            // Do NOT shift world environments (e.g. Seoul City) - their roads/streets are already authored at Y=0!
            bool isEnv = (filepath.find("City") != std::string::npos ||
                          filepath.find("Seoul") != std::string::npos ||
                          filepath.find("Map") != std::string::npos ||
                          filepath.find("World") != std::string::npos ||
                          filepath.find("street") != std::string::npos);
            if (!isEnv) {
                float minY = outInstance.activeDeformedMesh->worldBounds.min.y;
                if (fabs(minY) > 0.001f) {
                    for (auto& v : outInstance.activeDeformedMesh->vertices) {
                        v.position.y -= minY;
                    }
                    outInstance.activeDeformedMesh->CalculateBounds();
                }
            }

            outInstance.bindVertices.resize(outInstance.activeDeformedMesh->vertices.size());
            for (size_t i = 0; i < outInstance.activeDeformedMesh->vertices.size(); ++i) {
                const auto& v = outInstance.activeDeformedMesh->vertices[i];
                SkinnedVertex3D sv;
                sv.position = v.position;
                sv.normal = v.normal;
                sv.u = v.u;
                sv.v = v.v;
                sv.color = v.color;
                outInstance.bindVertices[i] = sv;
            }

            outInstance.activeDeformedMesh->BuildClusters();
            cgltf_free(data);
            return true;
        }

        // 5. Parse Scene Nodes & Mesh Primitives (supports multi-skin + bone-attached rigid accessories)
        std::vector<int> rigidNodeSkinSlot(data->nodes_count, -1);
        auto getOrCreateRigidBoneSlot = [&](int nodeIdx) -> int {
            if (nodeIdx < 0 || nodeIdx >= (int)data->nodes_count) return 0;
            if (rigidNodeSkinSlot[nodeIdx] >= 0) return rigidNodeSkinSlot[nodeIdx];
            int slot = static_cast<int>(outInstance.skinJointNodeIndices.size());
            outInstance.skinJointNodeIndices.push_back(nodeIdx);
            outInstance.skinJointIBMs.push_back(Mat4::Identity());
            rigidNodeSkinSlot[nodeIdx] = slot;
            return slot;
        };

        auto isStudioFloorHelper = [](const char* nName) -> bool {
            if (!nName) return false;
            std::string s = nName;
            for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            return (s.find("floor") != std::string::npos ||
                    s.find("backdrop") != std::string::npos ||
                    s.find("studio") != std::string::npos ||
                    s.find("stage") != std::string::npos ||
                    s.find("pedestal") != std::string::npos ||
                    s.find("fog") != std::string::npos);
        };

        uint32_t vertexBase = 0;
        for (cgltf_size nIdx = 0; nIdx < data->nodes_count; ++nIdx) {
            cgltf_node* gNode = &data->nodes[nIdx];
            if (!gNode->mesh) continue;
            if (isStudioFloorHelper(gNode->name)) continue;

            const cgltf_mesh& gMesh = *gNode->mesh;
            size_t nodeSkinOffset = 0;
            if (gNode->skin) {
                size_t sIdx = static_cast<size_t>(gNode->skin - data->skins);
                if (sIdx < skinOffsets.size()) nodeSkinOffset = skinOffsets[sIdx];
            }

            for (cgltf_size p = 0; p < gMesh.primitives_count; ++p) {
                const cgltf_primitive& prim = gMesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles) continue;

                SubMesh sub;
                sub.indexOffset = static_cast<uint32_t>(outInstance.activeDeformedMesh->indices.size());
                float baseFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

                if (prim.material) {
                    sub.emissive = ColorRGB(
                        prim.material->emissive_factor[0],
                        prim.material->emissive_factor[1],
                        prim.material->emissive_factor[2]
                    );
                    if (prim.material->has_emissive_strength) {
                        sub.emissiveStrength = prim.material->emissive_strength.emissive_strength;
                    }
                    sub.doubleSided = (prim.material->double_sided != 0);
                    sub.alphaCutoff = prim.material->alpha_cutoff;
                    if (prim.material->alpha_mode == cgltf_alpha_mode_mask) sub.alphaMode = PBRMaterial::AlphaMode::MASK_MODE;
                    else if (prim.material->alpha_mode == cgltf_alpha_mode_blend) sub.alphaMode = PBRMaterial::AlphaMode::BLEND_MODE;

                    if (prim.material->normal_texture.texture && prim.material->normal_texture.texture->image) {
                        RawTex rt = getTex(prim.material->normal_texture.texture->image);
                        sub.normalTexW = rt.w; sub.normalTexH = rt.h; sub.normalTexRGBA = rt.rgba;
                    }
                    if (prim.material->occlusion_texture.texture && prim.material->occlusion_texture.texture->image) {
                        RawTex rt = getTex(prim.material->occlusion_texture.texture->image);
                        sub.aoTexW = rt.w; sub.aoTexH = rt.h; sub.aoTexRGBA = rt.rgba;
                    }
                    if (prim.material->emissive_texture.texture && prim.material->emissive_texture.texture->image) {
                        RawTex rt = getTex(prim.material->emissive_texture.texture->image);
                        sub.emissiveTexW = rt.w; sub.emissiveTexH = rt.h; sub.emissiveTexRGBA = rt.rgba;
                    }

                    if (prim.material->has_pbr_metallic_roughness) {
                        const auto& pbr = prim.material->pbr_metallic_roughness;
                        if (pbr.base_color_factor[3] < 0.05f && (!pbr.base_color_texture.texture || !pbr.base_color_texture.texture->image)) {
                            continue;
                        }
                        baseFactor[0] = pbr.base_color_factor[0];
                        baseFactor[1] = pbr.base_color_factor[1];
                        baseFactor[2] = pbr.base_color_factor[2];
                        baseFactor[3] = pbr.base_color_factor[3];
                        sub.albedo = ColorRGB(baseFactor[0], baseFactor[1], baseFactor[2]);
                        sub.metallic = pbr.metallic_factor;
                        sub.roughness = pbr.roughness_factor;

                        if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                            RawTex rt = getTex(pbr.base_color_texture.texture->image);
                            sub.texWidth = rt.w;
                            sub.texHeight = rt.h;
                            sub.texRGBA = rt.rgba;
                        }
                        if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image) {
                            RawTex rt = getTex(pbr.metallic_roughness_texture.texture->image);
                            sub.mrTexW = rt.w; sub.mrTexH = rt.h; sub.mrTexRGBA = rt.rgba;
                        }
                    } else if (prim.material->has_pbr_specular_glossiness) {
                        const auto& sg = prim.material->pbr_specular_glossiness;
                        if (sg.diffuse_factor[3] < 0.05f && (!sg.diffuse_texture.texture || !sg.diffuse_texture.texture->image)) {
                            continue;
                        }
                        baseFactor[0] = sg.diffuse_factor[0];
                        baseFactor[1] = sg.diffuse_factor[1];
                        baseFactor[2] = sg.diffuse_factor[2];
                        baseFactor[3] = sg.diffuse_factor[3];
                        sub.albedo = ColorRGB(baseFactor[0], baseFactor[1], baseFactor[2]);
                        sub.metallic = 0.08f;
                        sub.roughness = std::max(0.15f, 1.0f - sg.glossiness_factor);

                        if (sg.diffuse_texture.texture && sg.diffuse_texture.texture->image) {
                            RawTex rt = getTex(sg.diffuse_texture.texture->image);
                            sub.texWidth = rt.w;
                            sub.texHeight = rt.h;
                            sub.texRGBA = rt.rgba;
                        }
                    }
                }

                const cgltf_accessor* posAcc = nullptr;
                const cgltf_accessor* normAcc = nullptr;
                const cgltf_accessor* uvAcc = nullptr;
                const cgltf_accessor* jointAcc = nullptr;
                const cgltf_accessor* weightAcc = nullptr;

                for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                    const auto& attr = prim.attributes[a];
                    if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) jointAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) weightAcc = attr.data;
                }

                if (!posAcc) continue;

                // Bone-attached rigid meshes vs unparented studio helper quads
                bool attachRigidToNode = false;
                int rigidSlot = 0;
                if (!jointAcc) {
                    if (gNode->parent != nullptr) {
                        attachRigidToNode = true;
                        rigidSlot = getOrCreateRigidBoneSlot(static_cast<int>(nIdx));
                    } else {
                        continue; // Unparented static studio floor/prop inside skinned file
                    }
                }

                size_t startIdx = outInstance.bindVertices.size();
                outInstance.bindVertices.resize(startIdx + posAcc->count);

                for (cgltf_size v = 0; v < posAcc->count; ++v) {
                    SkinnedVertex3D& vert = outInstance.bindVertices[startIdx + v];

                    float pVal[3] = {0,0,0};
                    cgltf_accessor_read_float(posAcc, v, pVal, 3);
                    vert.position = Vec3(pVal[0], pVal[1], pVal[2]);

                    if (normAcc) {
                        float nVal[3] = {0, 1, 0};
                        cgltf_accessor_read_float(normAcc, v, nVal, 3);
                        vert.normal = Vec3(nVal[0], nVal[1], nVal[2]).Normalized();
                    }

                    if (uvAcc) {
                        float uv[2] = {0,0};
                        cgltf_accessor_read_float(uvAcc, v, uv, 2);
                        vert.u = uv[0]; vert.v = uv[1];
                    }

                    float colR = sub.albedo.r, colG = sub.albedo.g, colB = sub.albedo.b;
                    if (sub.texRGBA && !sub.texRGBA->empty() && uvAcc) {
                        float u_wrap = vert.u - std::floor(vert.u);
                        float v_wrap = vert.v - std::floor(vert.v);
                        int tx = std::max(0, std::min(sub.texWidth - 1, static_cast<int>(u_wrap * sub.texWidth)));
                        int ty = std::max(0, std::min(sub.texHeight - 1, static_cast<int>(v_wrap * sub.texHeight)));
                        int pIdx = (ty * sub.texWidth + tx) * 4;

                        colR *= (*sub.texRGBA)[pIdx + 0] / 255.0f;
                        colG *= (*sub.texRGBA)[pIdx + 1] / 255.0f;
                        colB *= (*sub.texRGBA)[pIdx + 2] / 255.0f;
                    }
                    vert.color = Vec4(colR, colG, colB, 1.0f);

                    if (attachRigidToNode) {
                        vert.joints[0] = static_cast<uint16_t>(rigidSlot);
                        vert.joints[1] = vert.joints[2] = vert.joints[3] = 0;
                        vert.weights[0] = 1.0f;
                        vert.weights[1] = vert.weights[2] = vert.weights[3] = 0.0f;
                    } else if (jointAcc) {
                        cgltf_uint j[4] = {0,0,0,0};
                        cgltf_accessor_read_uint(jointAcc, v, j, 4);
                        vert.joints[0] = static_cast<uint16_t>(nodeSkinOffset + j[0]);
                        vert.joints[1] = static_cast<uint16_t>(nodeSkinOffset + j[1]);
                        vert.joints[2] = static_cast<uint16_t>(nodeSkinOffset + j[2]);
                        vert.joints[3] = static_cast<uint16_t>(nodeSkinOffset + j[3]);

                        if (weightAcc) {
                            float w[4] = {1,0,0,0};
                            cgltf_accessor_read_float(weightAcc, v, w, 4);
                            float wsum = w[0] + w[1] + w[2] + w[3];
                            if (wsum > 1e-6f) {
                                float inv = 1.0f / wsum;
                                vert.weights[0] = w[0] * inv; vert.weights[1] = w[1] * inv;
                                vert.weights[2] = w[2] * inv; vert.weights[3] = w[3] * inv;
                            } else {
                                vert.weights[0] = 1.0f; vert.weights[1] = vert.weights[2] = vert.weights[3] = 0.0f;
                            }
                        }
                    }
                }

                if (prim.indices) {
                    sub.indexCount = static_cast<uint32_t>(prim.indices->count);
                    for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                        outInstance.activeDeformedMesh->indices.push_back(
                            vertexBase + static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i))
                        );
                    }
                } else {
                    sub.indexCount = static_cast<uint32_t>(posAcc->count);
                    for (cgltf_size i = 0; i < posAcc->count; ++i) {
                        outInstance.activeDeformedMesh->indices.push_back(vertexBase + static_cast<uint32_t>(i));
                    }
                }

                outInstance.activeDeformedMesh->submeshes.push_back(sub);
                vertexBase += static_cast<uint32_t>(posAcc->count);
            }
        }

        // Initialize deformed mesh vertices with bind pose
        outInstance.activeDeformedMesh->vertices.resize(outInstance.bindVertices.size());
        for (size_t i = 0; i < outInstance.bindVertices.size(); ++i) {
            const auto& b = outInstance.bindVertices[i];
            outInstance.activeDeformedMesh->vertices[i] = Vertex3D(b.position, b.normal, b.u, b.v, b.color);
        }
        outInstance.activeDeformedMesh->BuildClusters();

        // Run initial skeletal skinning pass to deform mesh to bind/frame 0 upright pose
        outInstance.Update(0.0f);

        cgltf_free(data);
        return true;
    }
};

} // namespace Surreal
