#pragma once
/**
 * ============================================================================
 * AION SURREAL 3D ENGINE - BARE-METAL & HYBRID MASTER ENGINE
 * The Sovereign 3D Game & Simulation Engine for CosmOS / MeinOS & Linux
 * Unites Data-Oriented ECS, Micro-Meshlets, PBR Cook-Torrance, and AION Mind
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include "surreal_meshlet.hpp"
#include "surreal_ecs.hpp"
#include <vector>
#include <chrono>
#include <iostream>

namespace Surreal {

class SurrealEngine {
public:
    SurrealWorld world;

    // Viewport & Framebuffer Configuration
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t screenPitch;
    uint32_t* framebuffer;
    float* depthBuffer;

    // Camera parameters
    Vec3 cameraPos;
    Vec3 cameraTarget;
    Vec3 cameraUp;
    float cameraFov;
    float cameraNear;
    float cameraFar;

    // Engine Statistics
    uint64_t renderedFrames;
    uint64_t renderedMeshlets;
    uint64_t culledMeshlets;
    float lastFrameTimeMs;

    SurrealEngine(uint32_t width = 1280, uint32_t height = 720)
        : screenWidth(width),
          screenHeight(height),
          screenPitch(width * sizeof(uint32_t)),
          framebuffer(nullptr),
          depthBuffer(nullptr),
          cameraPos(0, 3.0f, 7.0f),
          cameraTarget(0, 1.0f, 0),
          cameraUp(0, 1.0f, 0),
          cameraFov(60.0f * (M_PI / 180.0f)),
          cameraNear(0.1f),
          cameraFar(1000.0f),
          renderedFrames(0),
          renderedMeshlets(0),
          culledMeshlets(0),
          lastFrameTimeMs(0.0f)
    {
        depthBuffer = new float[screenWidth * screenHeight];
    }

    ~SurrealEngine() {
        if (depthBuffer) {
            delete[] depthBuffer;
            depthBuffer = nullptr;
        }
    }

    void AttachFramebuffer(uint32_t* fb, uint32_t pitch) {
        framebuffer = fb;
        screenPitch = pitch;
    }

    void Clear(uint32_t clearColor = 0xFF050811) {
        if (!framebuffer || !depthBuffer) return;
        #pragma omp parallel for schedule(static)
        for (uint32_t y = 0; y < screenHeight; ++y) {
            float t = static_cast<float>(y) / static_cast<float>(screenHeight);
            // Cosmic dusk gradient: deep starry zenith -> glowing horizon -> dark floor
            float r, g, b;
            if (t < 0.50f) {
                float k = t / 0.50f;
                r = 0.02f + k * 0.08f;
                g = 0.03f + k * 0.04f;
                b = 0.07f + k * 0.12f;
            } else {
                float k = (t - 0.50f) / 0.50f;
                r = 0.10f * (1.0f - k * 0.75f);
                g = 0.07f * (1.0f - k * 0.55f);
                b = 0.19f * (1.0f - k * 0.65f);
            }
            uint32_t cr = static_cast<uint32_t>(r * 255.0f);
            uint32_t cg = static_cast<uint32_t>(g * 255.0f);
            uint32_t cb = static_cast<uint32_t>(b * 255.0f);
            uint32_t skyPix = 0xFF000000 | (cr << 16) | (cg << 8) | cb;

            uint32_t rowStart = y * screenWidth;
            for (uint32_t x = 0; x < screenWidth; ++x) {
                framebuffer[rowStart + x] = skyPix;
                depthBuffer[rowStart + x] = 100000.0f;
            }
        }
    }

    Mat4 GetViewMatrix() const {
        return Mat4::LookAt(cameraPos, cameraTarget, cameraUp);
    }

    Mat4 GetProjectionMatrix() const {
        float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
        return Mat4::Perspective(cameraFov, aspect, cameraNear, cameraFar);
    }

    /**
     * Procedural Mesh Helpers: PBR Cube
     */
    static std::shared_ptr<SurrealMesh> CreateCube(float size) {
        auto mesh = std::make_shared<SurrealMesh>();
        float h = size * 0.5f;

        // 6 Faces, 24 vertices
        Vec3 normals[6] = {
            Vec3(0, 0, 1), Vec3(0, 0, -1),
            Vec3(-1, 0, 0), Vec3(1, 0, 0),
            Vec3(0, 1, 0), Vec3(0, -1, 0)
        };

        // Front Face
        mesh->vertices.push_back(Vertex3D(Vec3(-h, -h,  h), normals[0], 0, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h, -h,  h), normals[0], 1, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h,  h,  h), normals[0], 1, 1));
        mesh->vertices.push_back(Vertex3D(Vec3(-h,  h,  h), normals[0], 0, 1));

        // Back Face
        mesh->vertices.push_back(Vertex3D(Vec3( h, -h, -h), normals[1], 0, 0));
        mesh->vertices.push_back(Vertex3D(Vec3(-h, -h, -h), normals[1], 1, 0));
        mesh->vertices.push_back(Vertex3D(Vec3(-h,  h, -h), normals[1], 1, 1));
        mesh->vertices.push_back(Vertex3D(Vec3( h,  h, -h), normals[1], 0, 1));

        // Left Face
        mesh->vertices.push_back(Vertex3D(Vec3(-h, -h, -h), normals[2], 0, 0));
        mesh->vertices.push_back(Vertex3D(Vec3(-h, -h,  h), normals[2], 1, 0));
        mesh->vertices.push_back(Vertex3D(Vec3(-h,  h,  h), normals[2], 1, 1));
        mesh->vertices.push_back(Vertex3D(Vec3(-h,  h, -h), normals[2], 0, 1));

        // Right Face
        mesh->vertices.push_back(Vertex3D(Vec3( h, -h,  h), normals[3], 0, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h, -h, -h), normals[3], 1, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h,  h, -h), normals[3], 1, 1));
        mesh->vertices.push_back(Vertex3D(Vec3( h,  h,  h), normals[3], 0, 1));

        // Top Face
        mesh->vertices.push_back(Vertex3D(Vec3(-h,  h,  h), normals[4], 0, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h,  h,  h), normals[4], 1, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h,  h, -h), normals[4], 1, 1));
        mesh->vertices.push_back(Vertex3D(Vec3(-h,  h, -h), normals[4], 0, 1));

        // Bottom Face
        mesh->vertices.push_back(Vertex3D(Vec3(-h, -h, -h), normals[5], 0, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h, -h, -h), normals[5], 1, 0));
        mesh->vertices.push_back(Vertex3D(Vec3( h, -h,  h), normals[5], 1, 1));
        mesh->vertices.push_back(Vertex3D(Vec3(-h, -h,  h), normals[5], 0, 1));

        for (uint32_t i = 0; i < 6; ++i) {
            uint32_t base = i * 4;
            mesh->indices.push_back(base + 0);
            mesh->indices.push_back(base + 1);
            mesh->indices.push_back(base + 2);
            mesh->indices.push_back(base + 0);
            mesh->indices.push_back(base + 2);
            mesh->indices.push_back(base + 3);
        }

        mesh->BuildClusters();
        return mesh;
    }

    /**
     * Master Render Frame (Unreal-Hybrid Architecture)
     */
    void RenderFrame(float dt) {
        auto tStart = std::chrono::high_resolution_clock::now();

        // 1. Tick Physics & World State
        world.UpdatePhysics(dt);

        // 2. Setup Matrices & Camera Frustum
        Mat4 viewMat = GetViewMatrix();
        Mat4 projMat = GetProjectionMatrix();
        Mat4 vpMat = projMat * viewMat;
        Frustum frustum = Frustum::FromViewProjection(vpMat);

        renderedMeshlets = 0;
        culledMeshlets = 0;

        // 3. Clear Framebuffer & Depth
        Clear();

        // 4. Meshlet & PBR Geometry Pass
        for (const auto& pair : world.meshRenderers) {
            EntityID id = pair.first;
            const MeshRendererComponent& renderer = pair.second;
            if (!renderer.mesh) continue;

            auto itT = world.transforms.find(id);
            if (itT == world.transforms.end()) continue;
            const TransformComponent& transform = itT->second;

            Mat4 worldMat = transform.GetWorldMatrix();
            Mat4 mvpMat = vpMat * worldMat;

            // Iterate over all Meshlets (Hierarchical Culling)
            for (const auto& meshlet : renderer.mesh->meshlets) {
                Vec3 worldClusterCenter = worldMat.TransformPoint(meshlet.boundingSphereCenter);
                float maxScale = std::max(transform.scale.x, std::max(transform.scale.y, transform.scale.z));
                float worldClusterRadius = meshlet.boundingSphereRadius * maxScale;

                // Frustum Culling
                if (!frustum.IntersectsSphere(worldClusterCenter, worldClusterRadius)) {
                    culledMeshlets++;
                    continue;
                }

                renderedMeshlets++;

                // Rasterize Meshlet Triangles with Cook-Torrance PBR
                RasterizeMeshlet(renderer.mesh, meshlet, worldMat, mvpMat, renderer.material);
            }
        }

        renderedFrames++;
        auto tEnd = std::chrono::high_resolution_clock::now();
        lastFrameTimeMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
    }

    void DrawCustomMesh(
        const std::shared_ptr<SurrealMesh>& mesh,
        const Mat4& worldMat,
        const PBRMaterial& material)
    {
        if (!mesh) return;
        Mat4 vpMat = GetProjectionMatrix() * GetViewMatrix();
        Mat4 mvpMat = vpMat * worldMat;
        for (const auto& meshlet : mesh->meshlets) {
            RasterizeMeshlet(mesh, meshlet, worldMat, mvpMat, material);
        }
    }

private:
    void RasterizeMeshlet(
        const std::shared_ptr<SurrealMesh>& mesh,
        const Meshlet& meshlet,
        const Mat4& worldMat,
        const Mat4& mvpMat,
        const PBRMaterial& material)
    {
        if (!framebuffer || !depthBuffer) return;

        Vec3 viewDir = (cameraPos - cameraTarget).Normalized();

        for (uint32_t t = 0; t < meshlet.triangleCount; ++t) {
            uint32_t i0 = mesh->indices[meshlet.triangleOffset + t * 3 + 0];
            uint32_t i1 = mesh->indices[meshlet.triangleOffset + t * 3 + 1];
            uint32_t i2 = mesh->indices[meshlet.triangleOffset + t * 3 + 2];

            const Vertex3D& v0 = mesh->vertices[i0];
            const Vertex3D& v1 = mesh->vertices[i1];
            const Vertex3D& v2 = mesh->vertices[i2];

            // Transform positions
            Vec3 w0 = worldMat.TransformPoint(v0.position);
            Vec3 w1 = worldMat.TransformPoint(v1.position);
            Vec3 w2 = worldMat.TransformPoint(v2.position);

            // Screen space projection with near-plane guard
            Vec4 c0 = mvpMat.TransformVec4(Vec4(v0.position.x, v0.position.y, v0.position.z, 1.0f));
            Vec4 c1 = mvpMat.TransformVec4(Vec4(v1.position.x, v1.position.y, v1.position.z, 1.0f));
            Vec4 c2 = mvpMat.TransformVec4(Vec4(v2.position.x, v2.position.y, v2.position.z, 1.0f));

            // Near-plane culling: reject triangles behind or piercing the near plane (prevents inverted-W scanlines)
            constexpr float W_NEAR = 0.15f;
            if (c0.w < W_NEAR || c1.w < W_NEAR || c2.w < W_NEAR) {
                continue;
            }

            float invW0 = 1.0f / c0.w;
            float invW1 = 1.0f / c1.w;
            float invW2 = 1.0f / c2.w;

            Vec3 p0(c0.x * invW0, c0.y * invW0, c0.z * invW0);
            Vec3 p1(c1.x * invW1, c1.y * invW1, c1.z * invW1);
            Vec3 p2(c2.x * invW2, c2.y * invW2, c2.z * invW2);

            // Screen Mapping
            float halfW = screenWidth * 0.5f;
            float halfH = screenHeight * 0.5f;

            float sx0 = (p0.x + 1.0f) * halfW;
            float sy0 = (1.0f - p0.y) * halfH;
            float sx1 = (p1.x + 1.0f) * halfW;
            float sy1 = (1.0f - p1.y) * halfH;
            float sx2 = (p2.x + 1.0f) * halfW;
            float sy2 = (1.0f - p2.y) * halfH;

            // Bounding box on screen
            int minX = std::max(0, (int)std::min(sx0, std::min(sx1, sx2)));
            int maxX = std::min((int)screenWidth - 1, (int)std::max(sx0, std::max(sx1, sx2)));
            int minY = std::max(0, (int)std::min(sy0, std::min(sy1, sy2)));
            int maxY = std::min((int)screenHeight - 1, (int)std::max(sy0, std::max(sy1, sy2)));

            float area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
            if (fabsf(area) <= 0.0001f) continue; // Cull only degenerate zero-area triangles

            // Two-sided rendering: flip winding for backfacing triangles
            if (area < 0.0f) {
                std::swap(sx1, sx2);
                std::swap(sy1, sy2);
                std::swap(p1, p2);
                area = -area;
            }
            float invArea = 1.0f / area;

            // Transform normals to world space
            Vec3 wn0 = worldMat.TransformDirection(v0.normal).Normalized();
            Vec3 wn1 = worldMat.TransformDirection(v1.normal).Normalized();
            Vec3 wn2 = worldMat.TransformDirection(v2.normal).Normalized();

            // Direct Lighting at triangle centroid for ultra-fast software path
            Vec3 triCenter = (w0 + w1 + w2) * 0.333333f;
            Vec3 triNormal = (wn0 + wn1 + wn2).Normalized();

            // Two-sided lighting normal flip
            if (triNormal.Dot(viewDir) < 0.0f) {
                triNormal = triNormal * -1.0f;
            }

            // Modulate material albedo with average vertex color across triangle
            Vec4 triCol = (v0.color + v1.color + v2.color) * 0.333333f;
            PBRMaterial triMat = material;
            triMat.albedo = material.albedo * ColorRGB(triCol.x, triCol.y, triCol.z);

            ColorRGB shadedColor = world.ambientLight * triMat.albedo;
            shadedColor = shadedColor + PBRShadingCore::EvaluateDirectLight(
                triCenter, triNormal, viewDir,
                world.sunLight.direction, world.sunLight.color * world.sunLight.intensity,
                triMat
            );

            // Subtle floor ambient bounce and distance depth fog
            float groundBounce = std::max(-triNormal.y * 0.5f + 0.5f, 0.0f);
            shadedColor = shadedColor + ColorRGB(0.08f, 0.07f, 0.12f) * (groundBounce * 0.6f) * triMat.albedo;

            float triDist = (triCenter - cameraPos).Length();
            float fogFactor = std::clamp((triDist - 8.0f) / 38.0f, 0.0f, 1.0f);
            ColorRGB fogCol(0.03f, 0.04f, 0.09f);
            ColorRGB finalShaded = shadedColor * (1.0f - fogFactor) + fogCol * fogFactor;
            uint32_t finalPixel = finalShaded.ToRGBA8();

            // High-Speed Incremental Barycentric Rasterization
            float dx0 = -(sy2 - sy1) * invArea;
            float dy0 =  (sx2 - sx1) * invArea;
            float dx1 = -(sy0 - sy2) * invArea;
            float dy1 =  (sx0 - sx2) * invArea;
            float dx2 = -(sy1 - sy0) * invArea;
            float dy2 =  (sx1 - sx0) * invArea;

            float row_w0 = ((sx2 - sx1) * (minY - sy1) - (sy2 - sy1) * (minX - sx1)) * invArea;
            float row_w1 = ((sx0 - sx2) * (minY - sy2) - (sy0 - sy2) * (minX - sx2)) * invArea;
            float row_w2 = ((sx1 - sx0) * (minY - sy0) - (sy1 - sy0) * (minX - sx0)) * invArea;

            for (int y = minY; y <= maxY; ++y) {
                float w0 = row_w0;
                float w1 = row_w1;
                float w2 = row_w2;
                uint32_t rowOffset = y * screenWidth;

                for (int x = minX; x <= maxX; ++x) {
                    if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                        float z = w0 * p0.z + w1 * p1.z + w2 * p2.z;
                        uint32_t pIdx = rowOffset + x;
                        if (z < depthBuffer[pIdx]) {
                            depthBuffer[pIdx] = z;
                            framebuffer[pIdx] = finalPixel;
                        }
                    }
                    w0 += dx0;
                    w1 += dx1;
                    w2 += dx2;
                }
                row_w0 += dy0;
                row_w1 += dy1;
                row_w2 += dy2;
            }
        }
    }
};

} // namespace Surreal

