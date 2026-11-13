#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - MESHLET & HIERARCHICAL CLUSTER CORE
 * Inspired by Unreal Nanite & id Tech Virtual Geometry Architectures
 * Enables micro-polygon rendering, continuous LOD, and instant frustum culling
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdint.h>
#include <vector>

namespace Surreal {

// Maximum vertices and triangles per micro-meshlet (optimized for cache lines)
constexpr uint32_t MESHLET_MAX_VERTICES = 64;
constexpr uint32_t MESHLET_MAX_TRIANGLES = 124;

struct alignas(16) Vertex3D {
    Vec3 position;
    Vec3 normal;
    float u, v;
    Vec4 color;

    constexpr Vertex3D()
        : position(), normal(0, 1.0f, 0), u(0), v(0), color(1.0f, 1.0f, 1.0f, 1.0f) {}
    constexpr Vertex3D(const Vec3& p, const Vec3& n, float _u, float _v, const Vec4& c = Vec4(1,1,1,1))
        : position(p), normal(n), u(_u), v(_v), color(c) {}
};

struct MeshletTriangle {
    uint8_t i0, i1, i2;
};

/**
 * Cluster / Meshlet Unit
 * Self-contained geometry chunk with tight bounding sphere and cone for backface culling
 */
struct Meshlet {
    uint32_t vertexOffset;      // Offset into global mesh vertex buffer
    uint32_t vertexCount;
    uint32_t triangleOffset;    // Offset into global mesh index buffer
    uint32_t triangleCount;

    Vec3 boundingSphereCenter;
    float boundingSphereRadius;

    // Normal cone for cluster back-face culling (conservative half-space test in object space)
    Vec3 coneApex;
    Vec3 coneAxis;
    float coneCutoff;           // sin(halfAngle), or >= 1.0f if disabled

    // Cluster LOD Error bounds (in object space)
    float lodError;
    float lodSphereRadius;
    uint32_t submeshIndex;

    inline bool ShouldRenderAtDistance(float cameraDist, float targetScreenErrorPx, float screenHeight, float fovRad) const {
        // Project object-space geometric error to screen-space pixel threshold
        float projFactor = screenHeight / (2.0f * tanf(fovRad * 0.5f));
        float screenErrorPx = (lodError / std::max(cameraDist, 0.001f)) * projFactor;
        return screenErrorPx <= targetScreenErrorPx;
    }

    inline bool IsBackfaceCulled(const Vec3& localCameraPos) const {
        if (coneCutoff >= 1.0f) return false;
        Vec3 diff = boundingSphereCenter - localCameraPos;
        float distSq = diff.LengthSq();
        float r = boundingSphereRadius;
        if (distSq <= r * r + 1e-6f) return false;
        float dist = sqrtf(distSq);
        Vec3 dir = diff * (1.0f / dist);
        return dir.Dot(coneAxis) > coneCutoff + (1.0f + coneCutoff) * (r / dist);
    }
};

/**
 * Multi-Material SubMesh Partition for PBR Rendering & Texturing
 */
struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    ColorRGB albedo = ColorRGB(1.0f, 1.0f, 1.0f);
    ColorRGB emissive = ColorRGB(0.0f, 0.0f, 0.0f);
    float emissiveStrength = 1.0f;
    float metallic = 0.2f;
    float roughness = 0.5f;
    float ambientOcclusion = 1.0f;
    PBRMaterial::AlphaMode alphaMode = PBRMaterial::AlphaMode::OPAQUE_MODE;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;

    uint32_t textureId = 0;
    int texWidth = 0;
    int texHeight = 0;
    std::shared_ptr<std::vector<uint8_t>> texRGBA;

    uint32_t normalTextureId = 0;
    int normalTexW = 0;
    int normalTexH = 0;
    std::shared_ptr<std::vector<uint8_t>> normalTexRGBA;

    uint32_t mrTextureId = 0;
    int mrTexW = 0;
    int mrTexH = 0;
    std::shared_ptr<std::vector<uint8_t>> mrTexRGBA;

    uint32_t aoTextureId = 0;
    int aoTexW = 0;
    int aoTexH = 0;
    std::shared_ptr<std::vector<uint8_t>> aoTexRGBA;

    uint32_t emissiveTextureId = 0;
    int emissiveTexW = 0;
    int emissiveTexH = 0;
    std::shared_ptr<std::vector<uint8_t>> emissiveTexRGBA;
};

/**
 * High-Density Surreal Mesh with Meshlet Hierarchy
 */
class SurrealMesh {
public:
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    std::vector<Meshlet> meshlets;
    std::vector<SubMesh> submeshes;
    AABB worldBounds;

    void CalculateBounds() {
        worldBounds = AABB();
        for (const auto& v : vertices) {
            worldBounds.Expand(v.position);
        }
    }

    static inline uint32_t ExpandBits10(uint32_t v) {
        v &= 0x3FFu;
        v = (v | (v << 16)) & 0x030000FFu;
        v = (v | (v << 8))  & 0x0300F00Fu;
        v = (v | (v << 4))  & 0x030C30C3u;
        v = (v | (v << 2))  & 0x09249249u;
        return v;
    }

    static inline uint32_t Morton3D(float nx, float ny, float nz) {
        uint32_t ix = static_cast<uint32_t>(std::clamp(nx, 0.0f, 1.0f) * 1023.0f);
        uint32_t iy = static_cast<uint32_t>(std::clamp(ny, 0.0f, 1.0f) * 1023.0f);
        uint32_t iz = static_cast<uint32_t>(std::clamp(nz, 0.0f, 1.0f) * 1023.0f);
        return ExpandBits10(ix) | (ExpandBits10(iy) << 1) | (ExpandBits10(iz) << 2);
    }

    /**
     * Build spatially coherent Meshlets from Triangle Stream (with normal cone culling bounds)
     */
    void BuildClusters() {
        meshlets.clear();
        if (indices.size() < 3 || vertices.empty()) {
            CalculateBounds();
            return;
        }

        CalculateBounds();
        uint32_t totalTris = static_cast<uint32_t>(indices.size() / 3);

        auto buildRange = [&](uint32_t startTri, uint32_t triCount, uint32_t submeshIdx) {
            if (triCount == 0 || startTri >= totalTris) return;
            triCount = std::min(triCount, totalTris - startTri);

            if (triCount > 1) {
                AABB centroidBox;
                std::vector<std::pair<uint32_t, uint32_t>> triKeys;
                triKeys.reserve(triCount);

                for (uint32_t t = 0; t < triCount; ++t) {
                    uint32_t base = (startTri + t) * 3;
                    uint32_t i0 = indices[base + 0];
                    uint32_t i1 = indices[base + 1];
                    uint32_t i2 = indices[base + 2];
                    if (i0 < vertices.size() && i1 < vertices.size() && i2 < vertices.size()) {
                        Vec3 c = (vertices[i0].position + vertices[i1].position + vertices[i2].position) * (1.0f / 3.0f);
                        centroidBox.Expand(c);
                    }
                }
                Vec3 ext = centroidBox.max - centroidBox.min;
                float invX = ext.x > 1e-6f ? 1.0f / ext.x : 0.0f;
                float invY = ext.y > 1e-6f ? 1.0f / ext.y : 0.0f;
                float invZ = ext.z > 1e-6f ? 1.0f / ext.z : 0.0f;

                for (uint32_t t = 0; t < triCount; ++t) {
                    uint32_t base = (startTri + t) * 3;
                    uint32_t i0 = indices[base + 0];
                    uint32_t i1 = indices[base + 1];
                    uint32_t i2 = indices[base + 2];
                    uint32_t code = 0;
                    if (i0 < vertices.size() && i1 < vertices.size() && i2 < vertices.size()) {
                        Vec3 c = (vertices[i0].position + vertices[i1].position + vertices[i2].position) * (1.0f / 3.0f);
                        code = Morton3D((c.x - centroidBox.min.x) * invX,
                                        (c.y - centroidBox.min.y) * invY,
                                        (c.z - centroidBox.min.z) * invZ);
                    }
                    triKeys.emplace_back(code, t);
                }

                std::sort(triKeys.begin(), triKeys.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });

                std::vector<uint32_t> sorted(triCount * 3);
                for (uint32_t t = 0; t < triCount; ++t) {
                    uint32_t srcBase = (startTri + triKeys[t].second) * 3;
                    sorted[t * 3 + 0] = indices[srcBase + 0];
                    sorted[t * 3 + 1] = indices[srcBase + 1];
                    sorted[t * 3 + 2] = indices[srcBase + 2];
                }
                std::copy(sorted.begin(), sorted.end(), indices.begin() + static_cast<ptrdiff_t>(startTri) * 3);
            }

            uint32_t localTri = 0;
            while (localTri < triCount) {
                uint32_t batchTris = std::min(MESHLET_MAX_TRIANGLES, triCount - localTri);
                Meshlet m = {};
                m.vertexOffset = 0;
                m.vertexCount = 0;
                m.triangleOffset = (startTri + localTri) * 3;
                m.triangleCount = batchTris;
                m.submeshIndex = submeshIdx;

                AABB clusterBox;
                Vec3 accumNorm(0.0f, 0.0f, 0.0f);
                std::vector<Vec3> faceNormals;
                faceNormals.reserve(batchTris);

                for (uint32_t t = 0; t < batchTris; ++t) {
                    uint32_t base = m.triangleOffset + t * 3;
                    uint32_t i0 = indices[base + 0];
                    uint32_t i1 = indices[base + 1];
                    uint32_t i2 = indices[base + 2];
                    if (i0 < vertices.size() && i1 < vertices.size() && i2 < vertices.size()) {
                        const Vec3& p0 = vertices[i0].position;
                        const Vec3& p1 = vertices[i1].position;
                        const Vec3& p2 = vertices[i2].position;
                        clusterBox.Expand(p0);
                        clusterBox.Expand(p1);
                        clusterBox.Expand(p2);
                        Vec3 fn = (p1 - p0).Cross(p2 - p0).Normalized();
                        if (fn.LengthSq() > 1e-12f) {
                            accumNorm = accumNorm + fn;
                            faceNormals.push_back(fn);
                        }
                    }
                }

                Vec3 center = clusterBox.Center();
                float maxR2 = 0.0f;
                for (uint32_t t = 0; t < batchTris; ++t) {
                    uint32_t base = m.triangleOffset + t * 3;
                    for (int k = 0; k < 3; ++k) {
                        uint32_t idx = indices[base + k];
                        if (idx < vertices.size()) {
                            float d2 = (vertices[idx].position - center).LengthSq();
                            if (d2 > maxR2) maxR2 = d2;
                        }
                    }
                }

                m.boundingSphereCenter = center;
                m.boundingSphereRadius = sqrtf(maxR2) + 1e-4f;
                m.lodError = m.boundingSphereRadius * 0.025f;
                m.lodSphereRadius = m.boundingSphereRadius;

                Vec3 avgN = accumNorm.Normalized();
                if (avgN.LengthSq() > 1e-12f && !faceNormals.empty()) {
                    float minDot = 1.0f;
                    for (const Vec3& fn : faceNormals) {
                        minDot = std::min(minDot, fn.Dot(avgN));
                    }
                    if (minDot > 0.05f) {
                        float sinTheta = sqrtf(std::max(0.0f, 1.0f - minDot * minDot));
                        m.coneApex = center;
                        m.coneAxis = avgN;
                        m.coneCutoff = sinTheta;
                    } else {
                        m.coneApex = center;
                        m.coneAxis = Vec3(0.0f, 1.0f, 0.0f);
                        m.coneCutoff = 1.1f;
                    }
                } else {
                    m.coneApex = center;
                    m.coneAxis = Vec3(0.0f, 1.0f, 0.0f);
                    m.coneCutoff = 1.1f;
                }

                meshlets.push_back(m);
                localTri += batchTris;
            }
        };

        if (submeshes.empty()) {
            buildRange(0, totalTris, 0);
        } else {
            for (size_t si = 0; si < submeshes.size(); ++si) {
                uint32_t startTri = submeshes[si].indexOffset / 3;
                uint32_t countTris = submeshes[si].indexCount / 3;
                buildRange(startTri, countTris, static_cast<uint32_t>(si));
            }
            if (meshlets.empty()) {
                buildRange(0, totalTris, 0);
            }
        }
    }
};

} // namespace Surreal

