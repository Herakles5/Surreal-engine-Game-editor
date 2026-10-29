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
