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
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace Surreal {

class SurrealEngine {
public:
    SurrealWorld world;

    // Viewport & Framebuffer Configuration
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t screenPitch;
    uint32_t* framebuffer;
    std::vector<float> depthBufferVec;
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
          depthBufferVec(static_cast<size_t>(width) * height, 100000.0f),
          depthBuffer(depthBufferVec.data()),
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
    }

    ~SurrealEngine() {
        depthBuffer = nullptr;
    }

    void AttachFramebuffer(uint32_t* fb, uint32_t pitch) {
        framebuffer = fb;
        screenPitch = pitch;
    }

    /**
     * Live viewport resize (window resize / fullscreen toggle).
     * Re-allocates the depth buffer; caller must re-attach its new framebuffer afterwards.
     */
    void Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) return;
        if (width == screenWidth && height == screenHeight) return;
        screenWidth = width;
        screenHeight = height;
        screenPitch = width * sizeof(uint32_t);
        depthBufferVec.assign(static_cast<size_t>(width) * height, 100000.0f);
        depthBuffer = depthBufferVec.data();
        framebuffer = nullptr; // must be re-attached by the caller
    }

    void Clear() {
        if (!framebuffer || depthBufferVec.empty()) return;
        depthBuffer = depthBufferVec.data();
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

    void Clear(uint32_t clearColor) {
        if (!framebuffer || depthBufferVec.empty()) return;
        depthBuffer = depthBufferVec.data();
        #pragma omp parallel for schedule(static)
        for (uint32_t y = 0; y < screenHeight; ++y) {
            uint32_t rowStart = y * screenWidth;
            for (uint32_t x = 0; x < screenWidth; ++x) {
                framebuffer[rowStart + x] = clearColor;
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

        // 4. Meshlet & PBR Geometry Pass (queued & strip-rasterized in parallel)
        std::vector<PreparedTriangle> frameTris;
        for (const auto& pair : world.meshRenderers) {
            EntityID id = pair.first;
            const MeshRendererComponent& renderer = pair.second;
            if (!renderer.mesh) continue;

            auto itT = world.transforms.find(id);
            if (itT == world.transforms.end()) continue;
            const TransformComponent& transform = itT->second;

            Mat4 worldMat = transform.GetWorldMatrix();
            Mat4 mvpMat = vpMat * worldMat;
            Mat4 normalMat = worldMat.InverseTranspose3x3();
            Vec3 localCamPos = worldMat.Inversed().TransformPoint(cameraPos);
            float maxScale = std::max(fabsf(transform.scale.x),
                                      std::max(fabsf(transform.scale.y), fabsf(transform.scale.z)));

            // Iterate over all Meshlets (Hierarchical Frustum + Normal Cone Culling)
            for (const auto& meshlet : renderer.mesh->meshlets) {
                Vec3 worldClusterCenter = worldMat.TransformPoint(meshlet.boundingSphereCenter);
                float worldClusterRadius = meshlet.boundingSphereRadius * maxScale;

                if (!frustum.IntersectsSphere(worldClusterCenter, worldClusterRadius)) {
                    culledMeshlets++;
                    continue;
                }

                const SubMesh* subPtr = (meshlet.submeshIndex < renderer.mesh->submeshes.size())
                    ? &renderer.mesh->submeshes[meshlet.submeshIndex]
                    : nullptr;
                bool isDoubleSided = renderer.material.doubleSided || (subPtr && subPtr->doubleSided);
                if (!isDoubleSided && meshlet.IsBackfaceCulled(localCamPos)) {
                    culledMeshlets++;
                    continue;
                }

                renderedMeshlets++;
                PrepareMeshletTriangles(renderer.mesh, meshlet, subPtr, worldMat, normalMat, mvpMat, renderer.material, frameTris);
            }
        }

        RasterizePreparedStrips(frameTris);

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
        Mat4 normalMat = worldMat.InverseTranspose3x3();
        std::vector<PreparedTriangle> customTris;
        for (const auto& meshlet : mesh->meshlets) {
            const SubMesh* subPtr = (meshlet.submeshIndex < mesh->submeshes.size())
                ? &mesh->submeshes[meshlet.submeshIndex]
                : nullptr;
            PrepareMeshletTriangles(mesh, meshlet, subPtr, worldMat, normalMat, mvpMat, material, customTris);
        }
        RasterizePreparedStrips(customTris);
    }

private:
    struct ClipVertex {
        Vec4 clip;
        Vec3 worldPos;
        Vec3 worldNorm;
        float u, v;
        Vec4 color;
    };

    struct PreparedTriangle {
        float sx0, sy0, pz0, invW0, u0, v0;
        float sx1, sy1, pz1, invW1, u1, v1;
        float sx2, sy2, pz2, invW2, u2, v2;
        float invArea;
        int minX, maxX, minY, maxY;
        const SubMesh* submesh;
        ColorRGB shadedBase;
        uint32_t flatPixel;
    };

    static inline uint32_t SampleTexturePixel(const SubMesh* sub, float u, float v, const ColorRGB& litTerm, float fogFactor, const ColorRGB& fogCol) {
        if (!sub || !sub->texRGBA || sub->texRGBA->empty() || sub->texWidth <= 0 || sub->texHeight <= 0) {
            ColorRGB c = litTerm * (1.0f - fogFactor) + fogCol * fogFactor;
            return ColorRGB::ACESFilm(c).LinearToSRGB().ToRGBA8();
        }
        float wu = u - floorf(u);
        float wv = v - floorf(v);
        int tx = std::clamp(static_cast<int>(wu * sub->texWidth), 0, sub->texWidth - 1);
        int ty = std::clamp(static_cast<int>(wv * sub->texHeight), 0, sub->texHeight - 1);
        size_t idx = static_cast<size_t>(ty * sub->texWidth + tx) * 4;
        const uint8_t* px = sub->texRGBA->data();
        // sRGB approx decode to linear
        float tr = powf(px[idx + 0] / 255.0f, 2.2f);
        float tg = powf(px[idx + 1] / 255.0f, 2.2f);
        float tb = powf(px[idx + 2] / 255.0f, 2.2f);
        ColorRGB combined(litTerm.r * tr, litTerm.g * tg, litTerm.b * tb);
        ColorRGB c = combined * (1.0f - fogFactor) + fogCol * fogFactor;
        return ColorRGB::ACESFilm(c).LinearToSRGB().ToRGBA8();
    }

    static ClipVertex LerpClipVertex(const ClipVertex& a, const ClipVertex& b, float t) {
        ClipVertex r;
        r.clip = a.clip + (b.clip - a.clip) * t;
        r.worldPos = a.worldPos + (b.worldPos - a.worldPos) * t;
        r.worldNorm = (a.worldNorm + (b.worldNorm - a.worldNorm) * t).Normalized();
        r.u = a.u + (b.u - a.u) * t;
        r.v = a.v + (b.v - a.v) * t;
        r.color = a.color + (b.color - a.color) * t;
        return r;
    }

    void PrepareMeshletTriangles(
        const std::shared_ptr<SurrealMesh>& mesh,
        const Meshlet& meshlet,
        const SubMesh* subPtr,
        const Mat4& worldMat,
        const Mat4& normalMat,
        const Mat4& mvpMat,
        const PBRMaterial& material,
        std::vector<PreparedTriangle>& outTris)
    {
        if (!framebuffer || !depthBuffer) return;

        constexpr float W_NEAR = 0.15f;
        const float halfW = screenWidth * 0.5f;
        const float halfH = screenHeight * 0.5f;
        const ColorRGB fogCol(0.03f, 0.04f, 0.09f);

        for (uint32_t t = 0; t < meshlet.triangleCount; ++t) {
            uint32_t base = meshlet.triangleOffset + t * 3;
            if (base + 2 >= mesh->indices.size()) break;
            uint32_t i0 = mesh->indices[base + 0];
            uint32_t i1 = mesh->indices[base + 1];
            uint32_t i2 = mesh->indices[base + 2];
            if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;

            const Vertex3D& v0 = mesh->vertices[i0];
            const Vertex3D& v1 = mesh->vertices[i1];
            const Vertex3D& v2 = mesh->vertices[i2];

            ClipVertex inVerts[3] = {
                {mvpMat.TransformVec4(Vec4(v0.position, 1.0f)), worldMat.TransformPoint(v0.position), normalMat.TransformDirection(v0.normal).Normalized(), v0.u, v0.v, v0.color},
                {mvpMat.TransformVec4(Vec4(v1.position, 1.0f)), worldMat.TransformPoint(v1.position), normalMat.TransformDirection(v1.normal).Normalized(), v1.u, v1.v, v1.color},
                {mvpMat.TransformVec4(Vec4(v2.position, 1.0f)), worldMat.TransformPoint(v2.position), normalMat.TransformDirection(v2.normal).Normalized(), v2.u, v2.v, v2.color}
            };

            if (inVerts[0].clip.w < W_NEAR && inVerts[1].clip.w < W_NEAR && inVerts[2].clip.w < W_NEAR) {
                continue;
            }

            // Homogeneous clip against near plane clip.w >= W_NEAR
            ClipVertex clipped[4];
            int clipCount = 0;
            if (inVerts[0].clip.w >= W_NEAR && inVerts[1].clip.w >= W_NEAR && inVerts[2].clip.w >= W_NEAR) {
                clipped[0] = inVerts[0];
                clipped[1] = inVerts[1];
                clipped[2] = inVerts[2];
                clipCount = 3;
            } else {
                for (int i = 0; i < 3; ++i) {
                    const ClipVertex& cur = inVerts[i];
                    const ClipVertex& prev = inVerts[(i + 2) % 3];
                    bool curIn = (cur.clip.w >= W_NEAR);
                    bool prevIn = (prev.clip.w >= W_NEAR);
                    if (curIn != prevIn) {
                        float denom = cur.clip.w - prev.clip.w;
                        float alpha = (fabsf(denom) > 1e-6f) ? ((W_NEAR - prev.clip.w) / denom) : 0.0f;
                        clipped[clipCount++] = LerpClipVertex(prev, cur, std::clamp(alpha, 0.0f, 1.0f));
                    }
                    if (curIn) {
                        clipped[clipCount++] = cur;
                    }
                }
            }

            if (clipCount < 3) continue;

            for (int fan = 1; fan + 1 < clipCount; ++fan) {
                ClipVertex tri[3] = {clipped[0], clipped[fan], clipped[fan + 1]};

                float invW0 = 1.0f / tri[0].clip.w;
                float invW1 = 1.0f / tri[1].clip.w;
                float invW2 = 1.0f / tri[2].clip.w;

                Vec3 p0(tri[0].clip.x * invW0, tri[0].clip.y * invW0, tri[0].clip.z * invW0);
                Vec3 p1(tri[1].clip.x * invW1, tri[1].clip.y * invW1, tri[1].clip.z * invW1);
                Vec3 p2(tri[2].clip.x * invW2, tri[2].clip.y * invW2, tri[2].clip.z * invW2);

                float sx0 = (p0.x + 1.0f) * halfW;
                float sy0 = (1.0f - p0.y) * halfH;
                float sx1 = (p1.x + 1.0f) * halfW;
                float sy1 = (1.0f - p1.y) * halfH;
                float sx2 = (p2.x + 1.0f) * halfW;
                float sy2 = (1.0f - p2.y) * halfH;

                float area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
                if (fabsf(area) <= 0.0001f) continue;

                if (area < 0.0f) {
                    std::swap(tri[1], tri[2]);
                    std::swap(sx1, sx2);
                    std::swap(sy1, sy2);
                    std::swap(p1, p2);
                    std::swap(invW1, invW2);
                    area = -area;
                }

                int minX = std::max(0, static_cast<int>(std::floor(std::min({sx0, sx1, sx2}))));
                int maxX = std::min(static_cast<int>(screenWidth) - 1, static_cast<int>(std::ceil(std::max({sx0, sx1, sx2}))));
                int minY = std::max(0, static_cast<int>(std::floor(std::min({sy0, sy1, sy2}))));
                int maxY = std::min(static_cast<int>(screenHeight) - 1, static_cast<int>(std::ceil(std::max({sy0, sy1, sy2}))));
                if (minX > maxX || minY > maxY) continue;

                Vec3 triCenter = (tri[0].worldPos + tri[1].worldPos + tri[2].worldPos) * (1.0f / 3.0f);
                Vec3 viewDir = (cameraPos - triCenter).Normalized();
                if (viewDir.LengthSq() <= 1e-12f) viewDir = Vec3(0.0f, 0.0f, 1.0f);

                Vec3 triNormal = (tri[0].worldNorm + tri[1].worldNorm + tri[2].worldNorm).Normalized();
                if (triNormal.LengthSq() <= 1e-12f) triNormal = Vec3(0.0f, 1.0f, 0.0f);
                if (triNormal.Dot(viewDir) < 0.0f) triNormal = triNormal * -1.0f;

                Vec4 triCol = (tri[0].color + tri[1].color + tri[2].color) * (1.0f / 3.0f);
                PBRMaterial triMat = material;
                if (subPtr) {
                    triMat.albedo = subPtr->albedo * material.albedo * ColorRGB(triCol.x, triCol.y, triCol.z);
                    triMat.roughness = std::clamp(subPtr->roughness, 0.04f, 1.0f);
                    triMat.metallic = std::clamp(subPtr->metallic, 0.0f, 1.0f);
                    triMat.ambientOcclusion = std::clamp(subPtr->ambientOcclusion * material.ambientOcclusion, 0.0f, 1.0f);
                    triMat.emissive = subPtr->emissive * subPtr->emissiveStrength + material.emissive;
                } else {
                    triMat.albedo = material.albedo * ColorRGB(triCol.x, triCol.y, triCol.z);
                }

                ColorRGB shadedColor = world.ambientLight * triMat.albedo * triMat.ambientOcclusion;
                shadedColor = shadedColor + PBRShadingCore::EvaluateDirectLight(
                    triCenter, triNormal, viewDir,
                    world.sunLight.direction, world.sunLight.color * world.sunLight.intensity,
                    triMat
                );

                // Evaluate dynamic PointLights in CPU software path
                for (const auto& pl : world.pointLights) {
                    Vec3 toLight = pl.position - triCenter;
                    float distSq = toLight.LengthSq();
                    float radiusSq = pl.radius * pl.radius;
                    if (distSq >= radiusSq || distSq <= 1e-8f) continue;
                    float dist = sqrtf(distSq);
                    float atten = 1.0f - (dist / std::max(pl.radius, 0.001f));
                    atten = atten * atten / (1.0f + 0.1f * distSq);
                    shadedColor = shadedColor + PBRShadingCore::EvaluateDirectLight(
                        triCenter, triNormal, viewDir,
                        toLight * (1.0f / dist), pl.color * (pl.intensity * atten),
                        triMat
                    );
                }

                float groundBounce = std::max(-triNormal.y * 0.5f + 0.5f, 0.0f);
                shadedColor = shadedColor + ColorRGB(0.08f, 0.07f, 0.12f) * (groundBounce * 0.6f) * triMat.albedo * triMat.ambientOcclusion;
                shadedColor = shadedColor + triMat.emissive;

                float triDist = (triCenter - cameraPos).Length();
                float fogFactor = std::clamp((triDist - 8.0f) / 38.0f, 0.0f, 1.0f);

                PreparedTriangle pt;
                pt.sx0 = sx0; pt.sy0 = sy0; pt.pz0 = p0.z; pt.invW0 = invW0; pt.u0 = tri[0].u; pt.v0 = tri[0].v;
                pt.sx1 = sx1; pt.sy1 = sy1; pt.pz1 = p1.z; pt.invW1 = invW1; pt.u1 = tri[1].u; pt.v1 = tri[1].v;
                pt.sx2 = sx2; pt.sy2 = sy2; pt.pz2 = p2.z; pt.invW2 = invW2; pt.u2 = tri[2].u; pt.v2 = tri[2].v;
                pt.invArea = 1.0f / area;
                pt.minX = minX; pt.maxX = maxX; pt.minY = minY; pt.maxY = maxY;
                pt.submesh = (subPtr && subPtr->texRGBA && !subPtr->texRGBA->empty()) ? subPtr : nullptr;
                // Keep lighting factor normalized if textured (divide by albedo safely), or full lit color if untextured
                if (pt.submesh) {
                    float r = shadedColor.r / std::max(triMat.albedo.r, 0.05f);
                    float g = shadedColor.g / std::max(triMat.albedo.g, 0.05f);
                    float b = shadedColor.b / std::max(triMat.albedo.b, 0.05f);
                    pt.shadedBase = ColorRGB(r, g, b);
                    pt.flatPixel = static_cast<uint32_t>(fogFactor * 65535.0f); // repurpose when textured? Keep fog explicit
                } else {
                    pt.shadedBase = shadedColor;
                    ColorRGB finalCol = shadedColor * (1.0f - fogFactor) + fogCol * fogFactor;
                    pt.flatPixel = ColorRGB::ACESFilm(finalCol).LinearToSRGB().ToRGBA8();
                }
                outTris.push_back(pt);
            }
        }
    }

    void RasterizePreparedStrips(const std::vector<PreparedTriangle>& tris) {
        if (!framebuffer || !depthBuffer || tris.empty() || screenHeight == 0) return;
        constexpr int STRIP_H = 16;
        const int numStrips = static_cast<int>((screenHeight + STRIP_H - 1) / STRIP_H);
        std::vector<std::vector<uint32_t>> bins(numStrips);
        for (uint32_t idx = 0; idx < tris.size(); ++idx) {
            int s0 = std::clamp(tris[idx].minY / STRIP_H, 0, numStrips - 1);
            int s1 = std::clamp(tris[idx].maxY / STRIP_H, 0, numStrips - 1);
            for (int s = s0; s <= s1; ++s) {
                bins[s].push_back(idx);
            }
        }

        const ColorRGB fogCol(0.03f, 0.04f, 0.09f);

        #pragma omp parallel for schedule(dynamic, 1)
        for (int s = 0; s < numStrips; ++s) {
            const auto& bin = bins[s];
            if (bin.empty()) continue;
            int stripMinY = s * STRIP_H;
            int stripMaxY = std::min(static_cast<int>(screenHeight) - 1, stripMinY + STRIP_H - 1);

            for (uint32_t triIdx : bin) {
                const PreparedTriangle& pt = tris[triIdx];
                int y0 = std::max(pt.minY, stripMinY);
                int y1 = std::min(pt.maxY, stripMaxY);
                if (y0 > y1) continue;

                float dx0 = -(pt.sy2 - pt.sy1) * pt.invArea;
                float dy0 =  (pt.sx2 - pt.sx1) * pt.invArea;
                float dx1 = -(pt.sy0 - pt.sy2) * pt.invArea;
                float dy1 =  (pt.sx0 - pt.sx2) * pt.invArea;
                float dx2 = -(pt.sy1 - pt.sy0) * pt.invArea;
                float dy2 =  (pt.sx1 - pt.sx0) * pt.invArea;

                float row_w0 = ((pt.sx2 - pt.sx1) * (y0 - pt.sy1) - (pt.sy2 - pt.sy1) * (pt.minX - pt.sx1)) * pt.invArea;
                float row_w1 = ((pt.sx0 - pt.sx2) * (y0 - pt.sy2) - (pt.sy0 - pt.sy2) * (pt.minX - pt.sx2)) * pt.invArea;
                float row_w2 = ((pt.sx1 - pt.sx0) * (y0 - pt.sy0) - (pt.sy1 - pt.sy0) * (pt.minX - pt.sx0)) * pt.invArea;

                float fogF = pt.submesh ? (static_cast<float>(pt.flatPixel) / 65535.0f) : 0.0f;

                for (int y = y0; y <= y1; ++y) {
                    float w0 = row_w0;
                    float w1 = row_w1;
                    float w2 = row_w2;
                    uint32_t rowOffset = static_cast<uint32_t>(y) * screenWidth;

                    for (int x = pt.minX; x <= pt.maxX; ++x) {
                        if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                            float z = w0 * pt.pz0 + w1 * pt.pz1 + w2 * pt.pz2;
                            uint32_t pIdx = rowOffset + static_cast<uint32_t>(x);
                            if (z < depthBuffer[pIdx]) {
                                depthBuffer[pIdx] = z;
                                if (pt.submesh) {
                                    float invW = w0 * pt.invW0 + w1 * pt.invW1 + w2 * pt.invW2;
                                    float u = (w0 * pt.u0 * pt.invW0 + w1 * pt.u1 * pt.invW1 + w2 * pt.u2 * pt.invW2) / std::max(invW, 1e-6f);
                                    float v = (w0 * pt.v0 * pt.invW0 + w1 * pt.v1 * pt.invW1 + w2 * pt.v2 * pt.invW2) / std::max(invW, 1e-6f);
                                    framebuffer[pIdx] = SampleTexturePixel(pt.submesh, u, v, pt.shadedBase, fogF, fogCol);
                                } else {
                                    framebuffer[pIdx] = pt.flatPixel;
                                }
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
    }
};

} // namespace Surreal

