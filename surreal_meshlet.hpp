#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - MESHLET & HIERARCHICAL CLUSTER CORE
 * Inspired by Unreal Nanite & id Tech Virtual Geometry Architectures
 * Enables micro-polygon rendering, continuous LOD, and instant frustum culling
 * ============================================================================
 */

#include "surreal_math.hpp"
#include <vector>
#include <stdint.h>

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

    // Normal cone for cluster back-face culling (Nanite / NVidia Mesh Shaders)
    Vec3 coneApex;
    Vec3 coneAxis;
    float coneCutoff;           // -cos(angle + pi/2)

    // Cluster LOD Error bounds (in object space)
    float lodError;
    float lodSphereRadius;

    inline bool ShouldRenderAtDistance(float cameraDist, float targetScreenErrorPx, float screenHeight, float fovRad) const {
        // Project object-space geometric error to screen-space pixel threshold
        float projFactor = screenHeight / (2.0f * tanf(fovRad * 0.5f));
        float screenErrorPx = (lodError / std::max(cameraDist, 0.001f)) * projFactor;
        return screenErrorPx <= targetScreenErrorPx;
    }
};

/**
 * High-Density Surreal Mesh with Meshlet Hierarchy
 */
class SurrealMesh {
public:
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    std::vector<Meshlet> meshlets;
    AABB worldBounds;

    void CalculateBounds() {
        worldBounds = AABB();
        for (const auto& v : vertices) {
            worldBounds.Expand(v.position);
        }
    }

    /**
     * Build Meshlets from Triangle Stream (Automatic Cluster Partitioning)
     */
    void BuildClusters() {
        meshlets.clear();
        if (indices.size() < 3) return;

        uint32_t triCount = static_cast<uint32_t>(indices.size() / 3);
        uint32_t currentTri = 0;

        while (currentTri < triCount) {
            Meshlet m = {};
            m.vertexOffset = 0; // In indexed representation
            m.triangleOffset = currentTri * 3;

            uint32_t batchTris = std::min(MESHLET_MAX_TRIANGLES, triCount - currentTri);
            m.triangleCount = batchTris;

            // Calculate cluster bounding sphere
            AABB clusterBox;
            for (uint32_t t = 0; t < batchTris; ++t) {
                uint32_t i0 = indices[(currentTri + t) * 3 + 0];
                uint32_t i1 = indices[(currentTri + t) * 3 + 1];
                uint32_t i2 = indices[(currentTri + t) * 3 + 2];
                if (i0 < vertices.size()) clusterBox.Expand(vertices[i0].position);
                if (i1 < vertices.size()) clusterBox.Expand(vertices[i1].position);
                if (i2 < vertices.size()) clusterBox.Expand(vertices[i2].position);
            }

            m.boundingSphereCenter = clusterBox.Center();
            m.boundingSphereRadius = clusterBox.BoundingRadius();
            m.lodError = m.boundingSphereRadius * 0.025f; // Initial heuristic
            m.lodSphereRadius = m.boundingSphereRadius;

            meshlets.push_back(m);
            currentTri += batchTris;
        }

        CalculateBounds();
    }
};

} // namespace Surreal

