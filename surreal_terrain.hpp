#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - 3D TERRAIN, OBSTACLES & ARENA GENERATOR
 * Generates Cyberpunk PBR Ground Floor, Parkour Jump Platforms & Monoliths
 * Integrated with AABB Obstacle Collision for Jump & Movement Physics
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include "surreal_meshlet.hpp"
#include <vector>
#include <string>
#include <memory>

namespace Surreal {

struct ObstacleBox {
    AABB aabb;
    std::string name;
    ColorRGB color;
    float topY;

    ObstacleBox(const Vec3& center, const Vec3& halfSize, const std::string& _name, const ColorRGB& c)
        : aabb(center - halfSize, center + halfSize),
          name(_name),
          color(c),
          topY(center.y + halfSize.y) {}
};

class SurrealTerrainBuilder {
public:
    /**
     * Create Massive Metropolis Ground Floor (180m x 180m = 32,400 m²)
     * Expansive asphalt street network with lane markings & cyber-grid accents.
     */
    static std::shared_ptr<SurrealMesh> CreateMetropolisGround(float size = 180.0f, int divisions = 60) {
        auto mesh = std::make_shared<SurrealMesh>();
        float half = size * 0.5f;
        float step = size / static_cast<float>(divisions);

        for (int z = 0; z < divisions; ++z) {
            for (int x = 0; x < divisions; ++x) {
                float x0 = -half + x * step;
                float z0 = -half + z * step;
                float x1 = x0 + step;
                float z1 = z0 + step;

                // Center avenue is within |z| < 15 and |x| < 45
                bool isCoreAvenue = (std::abs(z0 + step * 0.5f) < 18.0f && std::abs(x0 + step * 0.5f) < 55.0f);
                bool isRoadMarking = (isCoreAvenue && (std::abs(z0 + step * 0.5f) < 1.0f) && (x % 2 == 0));
                bool isCrossroad = (std::abs(x0 + step * 0.5f) < 6.0f && std::abs(z0 + step * 0.5f) < 45.0f);
                bool isOuterGrid = (std::abs(x0 + step * 0.5f) > 55.0f || std::abs(z0 + step * 0.5f) > 45.0f);

                Vec4 col;
                if (isRoadMarking) {
                    col = Vec4(0.95f, 0.85f, 0.25f, 1.0f); // Yellow road dashed line
                } else if (isCoreAvenue) {
                    // Dark realistic asphalt
                    bool noise = ((x * 7 + z * 13) % 5 == 0);
                    col = noise ? Vec4(0.14f, 0.14f, 0.16f, 1.0f) : Vec4(0.11f, 0.11f, 0.13f, 1.0f);
                } else if (isCrossroad) {
                    col = Vec4(0.12f, 0.12f, 0.14f, 1.0f);
                } else if (isOuterGrid) {
                    bool alt = ((x + z) % 2 == 0);
                    if ((x % 5 == 0) || (z % 5 == 0)) {
                        col = Vec4(0.04f, 0.35f, 0.45f, 1.0f); // Glowing cyan boundary grid line
                    } else if (alt) {
                        col = Vec4(0.08f, 0.10f, 0.14f, 1.0f);
                    } else {
                        col = Vec4(0.05f, 0.06f, 0.09f, 1.0f);
                    }
                } else {
                    col = Vec4(0.15f, 0.16f, 0.19f, 1.0f); // Sidewalk concrete
                }

                uint32_t base = static_cast<uint32_t>(mesh->vertices.size());
                mesh->vertices.push_back(Vertex3D(Vec3(x0, -0.005f, z0), Vec3(0, 1.0f, 0), x0 * 0.1f, z0 * 0.1f, col));
                mesh->vertices.push_back(Vertex3D(Vec3(x1, -0.005f, z0), Vec3(0, 1.0f, 0), x1 * 0.1f, z0 * 0.1f, col));
                mesh->vertices.push_back(Vertex3D(Vec3(x1, -0.005f, z1), Vec3(0, 1.0f, 0), x1 * 0.1f, z1 * 0.1f, col));
                mesh->vertices.push_back(Vertex3D(Vec3(x0, -0.005f, z1), Vec3(0, 1.0f, 0), x0 * 0.1f, z1 * 0.1f, col));

                mesh->indices.push_back(base + 0);
                mesh->indices.push_back(base + 1);
                mesh->indices.push_back(base + 2);
                mesh->indices.push_back(base + 0);
                mesh->indices.push_back(base + 2);
                mesh->indices.push_back(base + 3);
            }
        }

        mesh->BuildClusters();
        return mesh;
    }

    /**
     * Create Soft Ground Shadow Disc Mesh (Planar Real-Time Contact Shadow)
     */
    static std::shared_ptr<SurrealMesh> CreateShadowDisc(float radius = 0.55f, int segments = 24) {
        auto mesh = std::make_shared<SurrealMesh>();
        uint32_t centerIdx = static_cast<uint32_t>(mesh->vertices.size());
        // Center has high alpha (dark shadow core)
        mesh->vertices.push_back(Vertex3D(Vec3(0, 0, 0), Vec3(0, 1.0f, 0), 0.5f, 0.5f, Vec4(0.01f, 0.01f, 0.02f, 0.70f)));

        for (int i = 0; i < segments; ++i) {
            float angle = (float)i * 2.0f * M_PI / (float)segments;
            float px = std::cos(angle) * radius;
            float pz = std::sin(angle) * radius;
            float u = 0.5f + 0.5f * std::cos(angle);
            float v = 0.5f + 0.5f * std::sin(angle);
            // Outer rim fades softly to alpha 0
            mesh->vertices.push_back(Vertex3D(Vec3(px, 0, pz), Vec3(0, 1.0f, 0), u, v, Vec4(0.01f, 0.01f, 0.02f, 0.0f)));
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t curr = centerIdx + 1 + i;
            uint32_t next = centerIdx + 1 + ((i + 1) % segments);
            mesh->indices.push_back(centerIdx);
            mesh->indices.push_back(curr);
            mesh->indices.push_back(next);
        }

        mesh->BuildClusters();
        return mesh;
    }

    /**
     * Create Large PBR Grid Ground Floor (24m x 24m)
     */
    static std::shared_ptr<SurrealMesh> CreateCyberGridFloor(float size = 24.0f, int divisions = 24) {
        auto mesh = std::make_shared<SurrealMesh>();
        float half = size * 0.5f;
        float step = size / static_cast<float>(divisions);

        for (int z = 0; z < divisions; ++z) {
            for (int x = 0; x < divisions; ++x) {
                float x0 = -half + x * step;
                float z0 = -half + z * step;
                float x1 = x0 + step;
                float z1 = z0 + step;

                // Alternate grid panel styling (Checkerboard / Cyber lines)
                bool isAlt = ((x + z) % 2 == 0);
                Vec4 col;
                if ((x % 4 == 0) || (z % 4 == 0)) {
                    // Glowing cyan grid guide lines
                    col = Vec4(0.08f, 0.45f, 0.55f, 1.0f);
                } else if (isAlt) {
                    col = Vec4(0.20f, 0.24f, 0.32f, 1.0f);
                } else {
                    col = Vec4(0.10f, 0.13f, 0.18f, 1.0f);
                }

                uint32_t base = static_cast<uint32_t>(mesh->vertices.size());
                mesh->vertices.push_back(Vertex3D(Vec3(x0, 0, z0), Vec3(0, 1.0f, 0), 0, 0, col));
                mesh->vertices.push_back(Vertex3D(Vec3(x1, 0, z0), Vec3(0, 1.0f, 0), 1, 0, col));
                mesh->vertices.push_back(Vertex3D(Vec3(x1, 0, z1), Vec3(0, 1.0f, 0), 1, 1, col));
                mesh->vertices.push_back(Vertex3D(Vec3(x0, 0, z1), Vec3(0, 1.0f, 0), 0, 1, col));

                mesh->indices.push_back(base + 0);
                mesh->indices.push_back(base + 1);
                mesh->indices.push_back(base + 2);
                mesh->indices.push_back(base + 0);
                mesh->indices.push_back(base + 2);
                mesh->indices.push_back(base + 3);
            }
        }

        mesh->BuildClusters();
        return mesh;
    }

    /**
     * Create Box Mesh for Platforms & Obstacles
     */
    static std::shared_ptr<SurrealMesh> CreateBox(const Vec3& size, const ColorRGB& color) {
        auto mesh = std::make_shared<SurrealMesh>();
        Vec3 h = size * 0.5f;
        Vec4 c(color.r, color.g, color.b, 1.0f);

        Vec3 normals[6] = {
            Vec3(0, 0, 1), Vec3(0, 0, -1),
            Vec3(-1, 0, 0), Vec3(1, 0, 0),
            Vec3(0, 1, 0), Vec3(0, -1, 0)
        };

        // 6 Faces
        auto addFace = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const Vec3& n) {
            uint32_t base = static_cast<uint32_t>(mesh->vertices.size());
            mesh->vertices.push_back(Vertex3D(p0, n, 0, 0, c));
            mesh->vertices.push_back(Vertex3D(p1, n, 1, 0, c));
            mesh->vertices.push_back(Vertex3D(p2, n, 1, 1, c));
            mesh->vertices.push_back(Vertex3D(p3, n, 0, 1, c));
            mesh->indices.push_back(base + 0);
            mesh->indices.push_back(base + 1);
            mesh->indices.push_back(base + 2);
            mesh->indices.push_back(base + 0);
            mesh->indices.push_back(base + 2);
            mesh->indices.push_back(base + 3);
        };

        // Front
        addFace(Vec3(-h.x, -h.y,  h.z), Vec3( h.x, -h.y,  h.z), Vec3( h.x,  h.y,  h.z), Vec3(-h.x,  h.y,  h.z), normals[0]);
        // Back
        addFace(Vec3( h.x, -h.y, -h.z), Vec3(-h.x, -h.y, -h.z), Vec3(-h.x,  h.y, -h.z), Vec3( h.x,  h.y, -h.z), normals[1]);
        // Left
        addFace(Vec3(-h.x, -h.y, -h.z), Vec3(-h.x, -h.y,  h.z), Vec3(-h.x,  h.y,  h.z), Vec3(-h.x,  h.y, -h.z), normals[2]);
        // Right
        addFace(Vec3( h.x, -h.y,  h.z), Vec3( h.x, -h.y, -h.z), Vec3( h.x,  h.y, -h.z), Vec3( h.x,  h.y,  h.z), normals[3]);
        // Top
        addFace(Vec3(-h.x,  h.y,  h.z), Vec3( h.x,  h.y,  h.z), Vec3( h.x,  h.y, -h.z), Vec3(-h.x,  h.y, -h.z), normals[4]);
        // Bottom
        addFace(Vec3(-h.x, -h.y, -h.z), Vec3( h.x, -h.y, -h.z), Vec3( h.x, -h.y,  h.z), Vec3(-h.x, -h.y,  h.z), normals[5]);

        mesh->BuildClusters();
        return mesh;
    }
};

} // namespace Surreal
