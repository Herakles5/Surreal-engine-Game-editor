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
#ifndef CGLTF_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION
#endif
#include "cgltf.h"
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"
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
    std::vector<AnimationClip> clips;
    std::vector<SkinnedVertex3D> bindVertices;
    std::shared_ptr<SurrealMesh> activeDeformedMesh;

    int activeClipIndex;
    float currentTime;
    float playbackSpeed;
    bool isLooping;
    bool hasFootBaseOffset;
    float footBaseOffset;
    bool isZUpModel;

    SkeletalMeshInstance()
        : activeDeformedMesh(std::make_shared<SurrealMesh>()),
          activeClipIndex(0),
          currentTime(0.0f),
          playbackSpeed(1.0f),
          isLooping(true),
          hasFootBaseOffset(false),
          footBaseOffset(0.0f),
          isZUpModel(false) {}

    void PlayClip(int index, bool loop = true) {
        if (index >= 0 && index < (int)clips.size()) {
            activeClipIndex = index;
            currentTime = 0.0f;
            isLooping = loop;
        }
    }

    /**
     * Advance animation time, sample keyframe curves, update hierarchy, and deform mesh
     */
    void Update(float dt) {
        if (joints.empty() || bindVertices.empty()) return;

        // 1. Reset all joints to base bind pose
        for (auto& j : joints) {
            j.localTranslation = j.baseTranslation;
            j.localRotation = j.baseRotation;
            j.localScale = j.baseScale;
        }

        // 2. Sample current active clip
        if (activeClipIndex >= 0 && activeClipIndex < (int)clips.size()) {
            const AnimationClip& clip = clips[activeClipIndex];
            if (clip.duration > 0.0f) {
                currentTime += dt * playbackSpeed;
                if (currentTime > clip.duration) {
                    if (isLooping) {
                        currentTime = fmodf(currentTime, clip.duration);
                    } else {
                        currentTime = clip.duration;
                    }
                }

                for (const auto& chan : clip.channels) {
                    if (chan.jointIndex < 0 || chan.jointIndex >= (int)joints.size()) continue;
                    SkeletonJoint& joint = joints[chan.jointIndex];

                    if (chan.keyTimes.size() < 2) continue;

                    // Locate keyframe interval
                    for (size_t k = 0; k < chan.keyTimes.size() - 1; ++k) {
                        float t0 = chan.keyTimes[k];
                        float t1 = chan.keyTimes[k + 1];

                        if (currentTime >= t0 && currentTime <= t1) {
                            float factor = (t1 > t0) ? (currentTime - t0) / (t1 - t0) : 0.0f;

                            if (chan.target == ChannelTarget::TRANSLATION && k < chan.vecValues.size() - 1) {
                                joint.localTranslation = Lerp(chan.vecValues[k], chan.vecValues[k + 1], factor);
                                joint.hasExplicitMatrix = false;
                            } else if (chan.target == ChannelTarget::ROTATION && k < chan.quatValues.size() - 1) {
                                joint.localRotation = Slerp(chan.quatValues[k], chan.quatValues[k + 1], factor);
                                joint.hasExplicitMatrix = false;
                            } else if (chan.target == ChannelTarget::SCALE && k < chan.vecValues.size() - 1) {
                                joint.localScale = Lerp(chan.vecValues[k], chan.vecValues[k + 1], factor);
                                joint.hasExplicitMatrix = false;
                            }
                            break;
                        }
                    }
                }
            }
        }

        // 3. Forward Kinematics: Update Local & Global Joint Transforms (memoized O(N) evaluation)
        for (auto& j : joints) {
            if (!j.hasExplicitMatrix) {
                j.localMatrix = Mat4::Translation(j.localTranslation) *
                                Mat4::FromQuat(j.localRotation) *
                                Mat4::Scaling(j.localScale);
            }
        }

        std::vector<bool> evalDone(joints.size(), false);
        auto evalGlobal = [&](auto& self, int idx) -> void {
            if (evalDone[idx]) return;
            SkeletonJoint& j = joints[idx];
            if (j.parentIndex >= 0 && j.parentIndex < (int)joints.size()) {
                self(self, j.parentIndex);
                j.globalMatrix = joints[j.parentIndex].globalMatrix * j.localMatrix;
            } else {
                j.globalMatrix = j.localMatrix;
            }
            evalDone[idx] = true;
        };
        for (size_t i = 0; i < joints.size(); ++i) {
            evalGlobal(evalGlobal, static_cast<int>(i));
        }

        // 4. Compute Joint Matrix Palette
        if (!skinJointNodeIndices.empty()) {
            skinMatrices.resize(skinJointNodeIndices.size());
            for (size_t s = 0; s < skinJointNodeIndices.size(); ++s) {
                int nodeIdx = skinJointNodeIndices[s];
                if (nodeIdx >= 0 && nodeIdx < (int)joints.size()) {
                    skinMatrices[s] = joints[nodeIdx].globalMatrix * skinJointIBMs[s];
                    joints[nodeIdx].skinMatrix = skinMatrices[s];
                }
            }
        } else {
            skinMatrices.resize(joints.size());
            for (size_t i = 0; i < joints.size(); ++i) {
                joints[i].skinMatrix = joints[i].globalMatrix * joints[i].inverseBindMatrix;
                skinMatrices[i] = joints[i].skinMatrix;
            }
        }

        // 5. Linear Blend Skinning (LBS) on Mesh Vertices
        DeformMesh();
    }

private:
    void DeformMesh() {
        if (!activeDeformedMesh) return;
        const size_t vertCount = bindVertices.size();
        if (activeDeformedMesh->vertices.size() != vertCount) {
            activeDeformedMesh->vertices.resize(vertCount);
        }

        const size_t numSkinMatrices = skinMatrices.size();
        if (numSkinMatrices == 0) return;

        // Calibrate foot base offset once at initialization to place character feet precisely on Y=0
        if (!hasFootBaseOffset) {
            float minGroundY = 1e9f;
            for (size_t i = 0; i < vertCount; ++i) {
                const SkinnedVertex3D& b = bindVertices[i];
                Vec3 skinnedPos(0, 0, 0);
                for (int w = 0; w < 4; ++w) {
                    float weight = b.weights[w];
                    if (weight > 0.0001f && b.joints[w] < numSkinMatrices) {
                        const Mat4& skinM = skinMatrices[b.joints[w]];
                        skinnedPos = skinnedPos + (skinM.TransformPoint(b.position) * weight);
                    }
                }
                if (skinnedPos.y < minGroundY) minGroundY = skinnedPos.y;
            }
            footBaseOffset = minGroundY;
            hasFootBaseOffset = true;
        }

        const float footOffset = footBaseOffset;

        // Multi-threaded Linear Blend Skinning (LBS) via OpenMP
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < (int)vertCount; ++i) {
            const SkinnedVertex3D& b = bindVertices[i];
            Vec3 skinnedPos(0, 0, 0);
            Vec3 skinnedNorm(0, 0, 0);

            for (int w = 0; w < 4; ++w) {
                float weight = b.weights[w];
                if (weight > 0.0001f) {
                    uint16_t jIdx = b.joints[w];
                    if (jIdx < numSkinMatrices) {
                        const Mat4& skinM = skinMatrices[jIdx];
                        skinnedPos = skinnedPos + (skinM.TransformPoint(b.position) * weight);
                        skinnedNorm = skinnedNorm + (skinM.TransformDirection(b.normal) * weight);
                    }
                }
            }

            activeDeformedMesh->vertices[i].position = Vec3(skinnedPos.x, skinnedPos.y - footOffset, skinnedPos.z);
            activeDeformedMesh->vertices[i].normal = skinnedNorm.Normalized();
            activeDeformedMesh->vertices[i].u = b.u;
            activeDeformedMesh->vertices[i].v = b.v;
            activeDeformedMesh->vertices[i].color = b.color;
        }

        // Update bounds & clusters for fast rendering
        activeDeformedMesh->CalculateBounds();
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

        // 2. Parse Skin Joint Mapping & Inverse Bind Matrices
        if (data->skins_count > 0) {
            const cgltf_skin& skin = data->skins[0];
            outInstance.skinJointNodeIndices.resize(skin.joints_count);
            outInstance.skinJointIBMs.resize(skin.joints_count, Mat4::Identity());

            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                outInstance.skinJointNodeIndices[j] = static_cast<int>(skin.joints[j] - data->nodes);
                if (skin.inverse_bind_matrices) {
                    float ibm[16];
                    cgltf_accessor_read_float(skin.inverse_bind_matrices, j, ibm, 16);
                    for (int r = 0; r < 4; ++r) {
                        for (int c = 0; c < 4; ++c) {
                            outInstance.skinJointIBMs[j].m[r][c] = ibm[c * 4 + r];
                        }
                    }
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
                    if (prim.material && prim.material->has_pbr_metallic_roughness) {
                        const auto& pbr = prim.material->pbr_metallic_roughness;
                        if (pbr.base_color_factor[3] < 0.05f && (!pbr.base_color_texture.texture || !pbr.base_color_texture.texture->image)) {
                            continue; // Skip invisible collision or fog mesh
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

                    if (prim.material && prim.material->has_pbr_metallic_roughness) {
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
                            float nx = m[0]*ln[0] + m[4]*ln[1] + m[8]*ln[2];
                            float ny = m[1]*ln[0] + m[5]*ln[1] + m[9]*ln[2];
                            float nz = m[2]*ln[0] + m[6]*ln[1] + m[10]*ln[2];
                            float len = sqrtf(nx*nx + ny*ny + nz*nz);
                            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
                            vert.normal = Vec3(nx, ny, nz);
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

        // 5. Parse Mesh Vertices with Joint Indices & Weights for Skinned Avatars
        uint32_t vertexBase = 0;
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            const cgltf_mesh& gMesh = data->meshes[m];
            for (cgltf_size p = 0; p < gMesh.primitives_count; ++p) {
                const cgltf_primitive& prim = gMesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles) continue;

                SubMesh sub;
                sub.indexOffset = static_cast<uint32_t>(outInstance.activeDeformedMesh->indices.size());
                float baseFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

                if (prim.material && prim.material->has_pbr_metallic_roughness) {
                    const auto& pbr = prim.material->pbr_metallic_roughness;
                    if (pbr.base_color_factor[3] < 0.05f && (!pbr.base_color_texture.texture || !pbr.base_color_texture.texture->image)) {
                        continue; // Skip invisible volume / fog helper
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

                    // Sample Color from Diffuse/Albedo Texture for software fallback
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

                    if (jointAcc) {
                        cgltf_uint j[4] = {0,0,0,0};
                        cgltf_accessor_read_uint(jointAcc, v, j, 4);
                        vert.joints[0] = static_cast<uint16_t>(j[0]);
                        vert.joints[1] = static_cast<uint16_t>(j[1]);
                        vert.joints[2] = static_cast<uint16_t>(j[2]);
                        vert.joints[3] = static_cast<uint16_t>(j[3]);
                    }

                    if (weightAcc) {
                        float w[4] = {1,0,0,0};
                        cgltf_accessor_read_float(weightAcc, v, w, 4);
                        vert.weights[0] = w[0]; vert.weights[1] = w[1];
                        vert.weights[2] = w[2]; vert.weights[3] = w[3];
                    }
                }

                // Indices
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
