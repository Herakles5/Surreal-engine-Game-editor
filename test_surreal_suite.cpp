/**
 * ============================================================================
 * AION SURREAL 3D ENGINE - COMPREHENSIVE HEADLESS VERIFICATION SUITE
 * Verifies Packages 1, 2, 3 without requiring a display or OpenGL window:
 *   1. Math NaN guards, LookAt stability, InverseTranspose3x3 orthogonality
 *   2. PBR guards, roughness clamp, ACES / LinearToSRGB, ToRGBA8 NaN safety
 *   3. CPU Rasterizer near-plane clipping, Clear(color), point lights,
 *      Morton meshlets, normal cone backface culling, submesh isolation
 *   4. ECS Physics dt clamping, sphere/box overlap resolution, friction safety
 *   5. glTF hierarchical node transforms, .gltf/.glb in LoadCustomFile,
 *      SkeletalMeshInstance CrossFadeToClip & GPU matrix-only mode
 *   6. surreal_json parser/serializer & surreal_paths portability
 *   7. SurrealEditor SaveLevel/LoadLevel roundtrip (legacy v1 & v2),
 *      Undo/Redo stack, GPU eviction hook, transformed worldBounds SyncObstacles
 *   8. Non-blocking AionLink loopback socket handshake & bounded rx buffer
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include "surreal_meshlet.hpp"
#include "surreal_ecs.hpp"
#include "surreal_engine.hpp"
#include "surreal_gltf.hpp"
#include "surreal_animation.hpp"
#include "surreal_model_loader.hpp"
#include "surreal_editor.hpp"
#include "surreal_aion_link.hpp"
#include "surreal_json.hpp"
#include "surreal_paths.hpp"

#include <arpa/inet.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace Surreal;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " (" << (msg) << ")\n";     \
            ++g_failed;                                                                            \
        } else {                                                                                   \
            ++g_passed;                                                                            \
        }                                                                                          \
    } while (0)

static bool IsFiniteMat4(const Mat4& m) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(m.m[r][c])) return false;
        }
    }
    return true;
}

static bool IsFiniteVec3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static void TestMathAndPBR() {
    std::cout << "[*] 1. Testing Math Guards, InverseTranspose3x3 & PBR...\n";

    Vec3 z = Vec3(0.0f, 0.0f, 0.0f).Normalized();
    CHECK(IsFiniteVec3(z) && z.LengthSq() == 0.0f, "Zero vector Normalized stays zero and finite");

    Mat4 deg1 = Mat4::LookAt(Vec3(1, 2, 3), Vec3(1, 2, 3), Vec3(0, 1, 0));
    CHECK(IsFiniteMat4(deg1), "LookAt with eye == target produces finite matrix");

    Mat4 deg2 = Mat4::LookAt(Vec3(0, 0, 0), Vec3(0, 5, 0), Vec3(0, 1, 0));
    CHECK(IsFiniteMat4(deg2), "LookAt with forward collinear with up produces finite matrix");

    // Non-uniform scale + Y rotation normal orthogonality check
    Mat4 world = Mat4::FromQuat(Quat::FromAxisAngle(Vec3(0, 1, 0), 0.55f)) * Mat4::Scaling(Vec3(2.5f, 0.4f, 1.8f));
    Mat4 nMat = world.InverseTranspose3x3();
    Vec3 localTangent = Vec3(1.0f, 1.0f, 0.0f).Normalized();
    Vec3 localNormal = Vec3(-1.0f, 1.0f, 0.0f).Normalized(); // perpendicular in local space
    Vec3 worldTangent = world.TransformDirection(localTangent).Normalized();
    Vec3 worldNormal = nMat.TransformDirection(localNormal).Normalized();
    CHECK(std::fabs(worldTangent.Dot(worldNormal)) < 1e-4f,
          "InverseTranspose3x3 preserves tangent-normal orthogonality under non-uniform scale");

    // PBR zero-vector & NaN guards
    PBRMaterial degenerMat;
    degenerMat.albedo = ColorRGB(1, 1, 1);
    degenerMat.metallic = 0.5f;
    degenerMat.roughness = 0.0f;
    ColorRGB cNan = PBRShadingCore::EvaluateDirectLight(
        Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 0, 0), Vec3(0, 0, 0), ColorRGB(1, 1, 1), degenerMat);
    CHECK(std::isfinite(cNan.r) && std::isfinite(cNan.g) && std::isfinite(cNan.b),
          "EvaluateDirectLight handles degenerate vectors & roughness=0 cleanly");

    ColorRGB nanIn(std::numeric_limits<float>::quiet_NaN(), 2.0f, -0.5f, 1.0f);
    uint32_t rgba = nanIn.ToRGBA8();
    CHECK(((rgba >> 16) & 0xFF) == 0 && ((rgba >> 8) & 0xFF) == 255 && (rgba & 0xFF) == 0,
          "ToRGBA8 clamps out-of-range and maps NaN to 0");

    ColorRGB tonemapped = ColorRGB::ACESFilm(ColorRGB(1.0f, 0.5f, 0.1f)).LinearToSRGB();
    CHECK(tonemapped.r > tonemapped.g && tonemapped.g > tonemapped.b && tonemapped.r < 1.0f,
          "ACESFilm + LinearToSRGB preserves monotonicity and HDR compression");

    PBRMaterial mat;
    CHECK(!mat.allowWetPuddles && mat.alphaMode == PBRMaterial::AlphaMode::OPAQUE_MODE,
          "PBRMaterial defaults allowWetPuddles=false and OPAQUE_MODE");
}

static void TestCPURasterizerAndMeshlets() {
    std::cout << "[*] 2. Testing CPU Rasterizer, Near Clipping, Meshlets & Cone Culling...\n";

    constexpr int W = 128, H = 96;
    std::vector<uint32_t> fb(W * H, 0u);
    SurrealEngine engine(W, H);
    engine.AttachFramebuffer(fb.data(), W * sizeof(uint32_t));
    engine.cameraPos = Vec3(0.0f, 0.0f, 3.0f);
    engine.cameraTarget = Vec3(0.0f, 0.0f, 0.0f);

    engine.Clear(0xFF123456);
    CHECK(fb[0] == 0xFF123456 && fb[W * H - 1] == 0xFF123456, "Clear(clearColor) fills exact framebuffer color");

    // Near-plane clipping check: triangle straddling camera plane z=3.0 (v0 behind cam, v1/v2 in front), tilted cleanly off eye plane
    auto clipMesh = std::make_shared<SurrealMesh>();
    clipMesh->vertices = {
        Vertex3D(Vec3( 0.0f,  1.5f, 6.0f), Vec3(0, 0, 1), 0.5f, 0.0f, Vec4(1, 0, 0, 1)), // behind eye at z=3!
        Vertex3D(Vec3(-1.5f, -0.5f, 0.0f), Vec3(0, 0, 1), 0.0f, 1.0f, Vec4(0, 1, 0, 1)),
        Vertex3D(Vec3( 1.5f, -0.5f, 0.0f), Vec3(0, 0, 1), 1.0f, 1.0f, Vec4(0, 0, 1, 1)),
    };
    clipMesh->indices = {0, 1, 2};
    clipMesh->BuildClusters();

    engine.Clear(0xFF000000);
    PBRMaterial whiteMat;
    whiteMat.albedo = ColorRGB(1.0f, 1.0f, 1.0f);
    whiteMat.roughness = 0.6f;
    whiteMat.doubleSided = true;
    engine.DrawCustomMesh(clipMesh, Mat4::Identity(), whiteMat);

    // Count pixels drawn (should be drawn safely across lower portion without NaN/corrupt wraparound at top row)
    int litPixels = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if ((fb[y * W + x] & 0x00FFFFFF) != 0) ++litPixels;
        }
    }
    CHECK(litPixels > 50, "Near-plane clipped triangle produces valid pixels on screen");

    // Backface cone culling + Morton spatial sorting + submesh separation
    auto gridMesh = std::make_shared<SurrealMesh>();
    const int N = 16;
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            uint32_t base = static_cast<uint32_t>(gridMesh->vertices.size());
            Vec3 n(0.0f, 0.0f, 1.0f);
            float x0 = static_cast<float>(i), y0 = static_cast<float>(j);
            gridMesh->vertices.push_back(Vertex3D(Vec3(x0, y0, 0.0f), n, 0, 0, Vec4(1, 1, 1, 1)));
            gridMesh->vertices.push_back(Vertex3D(Vec3(x0 + 1, y0, 0.0f), n, 1, 0, Vec4(1, 1, 1, 1)));
            gridMesh->vertices.push_back(Vertex3D(Vec3(x0 + 1, y0 + 1, 0.0f), n, 1, 1, Vec4(1, 1, 1, 1)));
            gridMesh->vertices.push_back(Vertex3D(Vec3(x0, y0 + 1, 0.0f), n, 0, 1, Vec4(1, 1, 1, 1)));
            gridMesh->indices.push_back(base + 0);
            gridMesh->indices.push_back(base + 1);
            gridMesh->indices.push_back(base + 2);
            gridMesh->indices.push_back(base + 0);
            gridMesh->indices.push_back(base + 2);
            gridMesh->indices.push_back(base + 3);
        }
    }
    SubMesh smA;
    smA.indexOffset = 0;
    smA.indexCount = static_cast<uint32_t>(gridMesh->indices.size() / 2);
    smA.albedo = ColorRGB(1, 0, 0);
    SubMesh smB;
    smB.indexOffset = smA.indexCount;
    smB.indexCount = static_cast<uint32_t>(gridMesh->indices.size()) - smA.indexCount;
    smB.albedo = ColorRGB(0, 1, 0);
    gridMesh->submeshes = {smA, smB};
    gridMesh->BuildClusters();

    CHECK(gridMesh->meshlets.size() >= 2, "Grid builds multiple meshlets");
    bool hasSub0 = false, hasSub1 = false;
    for (const auto& m : gridMesh->meshlets) {
        if (m.submeshIndex == 0) hasSub0 = true;
        if (m.submeshIndex == 1) hasSub1 = true;
    }
    CHECK(hasSub0 && hasSub1, "BuildClusters preserves per-submesh meshlet partitioning");
    CHECK(!gridMesh->meshlets[0].IsBackfaceCulled(Vec3(8.0f, 8.0f, 50.0f)),
          "Front-facing camera does NOT backface cull +Z planar cluster");
    CHECK(gridMesh->meshlets[0].IsBackfaceCulled(Vec3(8.0f, 8.0f, -50.0f)),
          "Rear camera behind +Z planar cluster culls cluster via normal cone");

    // Point light lighting contribution check via world.RenderFrame
    auto box = SurrealTerrainBuilder::CreateBox(Vec3(2.0f, 2.0f, 2.0f), ColorRGB(0.8f, 0.8f, 0.8f));
    EntityID boxId = engine.world.CreateEntity("LitBox");
    engine.world.transforms[boxId].position = Vec3(0, 0, 0);
    engine.world.meshRenderers[boxId].mesh = box;
    engine.world.meshRenderers[boxId].material.albedo = ColorRGB(0.8f, 0.8f, 0.8f);
    engine.world.meshRenderers[boxId].material.roughness = 0.5f;
    engine.world.sunLight.intensity = 0.0f;
    engine.world.ambientLight = ColorRGB(0.0f, 0.0f, 0.0f);

    engine.RenderFrame(0.016f);
    uint64_t darkSum = 0;
    for (int y = 20; y < H - 20; ++y) {
        for (int x = 20; x < W - 20; ++x) {
            uint32_t px = fb[y * W + x];
            darkSum += ((px >> 16) & 0xFF) + ((px >> 8) & 0xFF) + (px & 0xFF);
        }
    }

    PointLight pl;
    pl.position = Vec3(0.0f, 0.5f, 2.5f);
    pl.color = ColorRGB(2.0f, 2.0f, 2.0f);
    pl.intensity = 4.0f;
    pl.radius = 12.0f;
    engine.world.pointLights.push_back(pl);
    engine.RenderFrame(0.016f);
    uint64_t litSum = 0;
    for (int y = 20; y < H - 20; ++y) {
        for (int x = 20; x < W - 20; ++x) {
            uint32_t px = fb[y * W + x];
            litSum += ((px >> 16) & 0xFF) + ((px >> 8) & 0xFF) + (px & 0xFF);
        }
    }
    CHECK(litSum > darkSum + 1000, "CPU rasterizer evaluates world.pointLights accurately");
}

static void TestECSPhysics() {
    std::cout << "[*] 3. Testing ECS Physics & Collider Resolution...\n";
    SurrealWorld world;

    EntityID a = world.CreateEntity("SphereA");
    world.transforms[a].position = Vec3(0.0f, 1.0f, 0.0f);
    world.rigidbodies[a].mass = 1.0f;
    world.rigidbodies[a].acceleration = Vec3(0, 0, 0);
    world.colliders[a].type = ColliderComponent::SPHERE;
    world.colliders[a].radius = 0.6f;

    EntityID b = world.CreateEntity("SphereB");
    world.transforms[b].position = Vec3(0.5f, 1.0f, 0.0f); // overlapping by 0.7m
    world.rigidbodies[b].mass = 1.0f;
    world.rigidbodies[b].acceleration = Vec3(0, 0, 0);
    world.colliders[b].type = ColliderComponent::SPHERE;
    world.colliders[b].radius = 0.6f;

    world.UpdatePhysics(0.016f);
    float dist = (world.transforms[b].position - world.transforms[a].position).Length();
    CHECK(dist >= 1.18f, "Overlapping spheres separate cleanly during UpdatePhysics");

    // Large dt clamp & friction non-negative stability (isolated world so no overlap with SphereA/SphereB)
    SurrealWorld clampWorld;
    EntityID c = clampWorld.CreateEntity("ClampBody");
    clampWorld.transforms[c].position = Vec3(0.0f, 0.5f, 0.0f);
    clampWorld.rigidbodies[c].velocity = Vec3(10.0f, 0.0f, 0.0f);
    clampWorld.colliders[c].type = ColliderComponent::SPHERE;
    clampWorld.colliders[c].radius = 0.5f;
    clampWorld.UpdatePhysics(10.0f);
    CHECK(IsFiniteVec3(clampWorld.transforms[c].position) && clampWorld.transforms[c].position.y >= 0.5f &&
          clampWorld.rigidbodies[c].velocity.x >= 0.0f,
          "UpdatePhysics clamps large dt and keeps ground friction stable");
}

static void TestGLTFHierarchyAndSkeletalCrossFade() {
    std::cout << "[*] 4. Testing glTF Hierarchy Baking, LoadCustomFile & CrossFadeToClip...\n";

    // Build a valid tiny 2-node hierarchical glTF file with embedded base64 data URI!
    // One triangle in local space (0..1, 0..1, 0), child node has translation [1,0,0], parent has translation [10,20,30].
    // Vertex positions (3 * 3 floats = 36 bytes) + indices (3 uint16 = 6 bytes padded to 8 = 44 bytes):
    // Wait: simpler! Write a real external .bin beside the .gltf in /tmp cleanly.
    const std::string gltfPath = "/tmp/surreal_test_hier.gltf";
    const std::string binPath = "/tmp/surreal_test_hier.bin";
    {
        float verts[9] = {0.f, 0.f, 0.f,  1.f, 0.f, 0.f,  0.f, 1.f, 0.f};
        uint16_t idx[4] = {0, 1, 2, 0};
        std::ofstream bf(binPath, std::ios::binary);
        bf.write(reinterpret_cast<const char*>(verts), sizeof(verts));
        bf.write(reinterpret_cast<const char*>(idx), sizeof(idx));
        bf.close();

        std::ofstream gf(gltfPath);
        gf << R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [
    { "name": "Parent", "translation": [10.0, 20.0, 30.0], "children": [1] },
    { "name": "Child",  "translation": [1.0,  2.0,  3.0],  "mesh": 0 }
  ],
  "meshes": [{
    "name": "TriMesh",
    "primitives": [{
      "attributes": { "POSITION": 0 },
      "indices": 1
    }]
  }],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
  ],
  "buffers": [{ "uri": "surreal_test_hier.bin", "byteLength": 44 }]
})";
        gf.close();
    }

    auto rawCombined = SurrealGLTFLoader::LoadCombinedMesh(gltfPath);
    CHECK(rawCombined && rawCombined->vertices.size() == 3,
          "SurrealGLTFLoader::LoadCombinedMesh loads .gltf hierarchy");
    if (rawCombined && !rawCombined->vertices.empty()) {
        Vec3 rv0 = rawCombined->vertices[0].position;
        CHECK(std::fabs(rv0.x - 11.0f) < 1e-4f && std::fabs(rv0.y - 22.0f) < 1e-4f && std::fabs(rv0.z - 33.0f) < 1e-4f,
              "glTF loader bakes full Parent+Child world transform hierarchy (10+1, 20+2, 30+3)");
    }

    std::shared_ptr<SurrealMesh> customMesh;
    bool okLoadCustom = SurrealModelLoader::LoadCustomFile(gltfPath, customMesh, ColorRGB(1, 1, 1));
    CHECK(okLoadCustom && customMesh && customMesh->vertices.size() == 3,
          "SurrealModelLoader::LoadCustomFile loads .gltf files cleanly");
    if (customMesh && !customMesh->vertices.empty()) {
        Vec3 v0 = customMesh->vertices[0].position;
        CHECK(std::fabs(v0.x - 11.0f) < 1e-4f && std::fabs(v0.y) < 1e-4f && std::fabs(v0.z - 33.0f) < 1e-4f,
              "LoadCustomFile preserves X/Z hierarchy and aligns min.y to floor 0");
    }

    // Also test real avatar scene.gltf if available
    std::string avatarPath = surreal::paths::GetDefaultAvatarPath();
    if (std::filesystem::exists(avatarPath)) {
        std::vector<GLTFModelNode> nodes;
        bool avatarOk = SurrealGLTFLoader::LoadModel(avatarPath, nodes);
        CHECK(avatarOk && !nodes.empty(), "Real avatar scene.gltf loads with LoadModel");
    }

    std::remove(gltfPath.c_str());
    std::remove(binPath.c_str());

    // Programmatic CrossFadeToClip test
    SkeletalMeshInstance skel;
    SkeletonJoint j0;
    j0.name = "Root";
    j0.parentIndex = -1;
    j0.baseTranslation = Vec3(0, 0, 0);
    j0.baseRotation = Quat(0, 0, 0, 1);
    j0.baseScale = Vec3(1, 1, 1);
    j0.inverseBindMatrix = Mat4::Identity();
    skel.joints.push_back(j0);
    skel.skinJointNodeIndices = {0};
    skel.skinJointIBMs = {Mat4::Identity()};

    SkinnedVertex3D sv;
    sv.position = Vec3(0, 1, 0);
    sv.normal = Vec3(0, 0, 1);
    sv.joints[0] = 0;
    sv.weights[0] = 1.0f;
    skel.bindVertices.push_back(sv);
    skel.activeDeformedMesh->vertices.push_back(Vertex3D(sv.position, sv.normal, 0, 0, Vec4(1, 1, 1, 1)));
    skel.activeDeformedMesh->indices = {0, 0, 0};

    AnimationClip clipA;
    clipA.name = "IdentityPose";
    clipA.duration = 1.0f;
    AnimationChannel chA;
    chA.jointIndex = 0;
    chA.target = ChannelTarget::ROTATION;
    chA.keyTimes = {0.0f, 1.0f};
    chA.quatValues = {Quat::FromAxisAngle(Vec3(0, 1, 0), 0.0f), Quat::FromAxisAngle(Vec3(0, 1, 0), 0.0f)};
    clipA.channels.push_back(chA);

    AnimationClip clipB;
    clipB.name = "Turn90";
    clipB.duration = 1.0f;
    AnimationChannel chB;
    chB.jointIndex = 0;
    chB.target = ChannelTarget::ROTATION;
    chB.keyTimes = {0.0f, 1.0f};
    chB.quatValues = {Quat::FromAxisAngle(Vec3(0, 1, 0), 1.5707963f), Quat::FromAxisAngle(Vec3(0, 1, 0), 1.5707963f)};
    clipB.channels.push_back(chB);

    skel.clips = {clipA, clipB};
    skel.Update(0.0f, true); // baseline Clip A
    Vec3 bindP = skel.activeDeformedMesh->vertices[0].position;

    skel.CrossFadeToClip(1, 0.4f, true);
    skel.Update(0.2f, false); // midpoint of 0.4s crossfade in GPU-matrix-only mode!
    Vec3 xAxisAtMid = skel.joints[0].globalMatrix.TransformDirection(Vec3(0, 0, 1)).Normalized();
    // Should be ~45 deg rotated between Z (0,0,1) and X (1,0,0) -> (~0.707, 0, ~0.707)
    CHECK(xAxisAtMid.x > 0.55f && xAxisAtMid.z > 0.55f,
          "CrossFadeToClip smoothly blends bone rotations midway across transition");
    CHECK(std::fabs(skel.activeDeformedMesh->vertices[0].position.x - bindP.x) < 1e-5f,
          "Update(dt, false) skips CPU vertex deformation in GPU skinning mode");
}

static void TestJsonEditorUndoAndRoundtrip() {
    std::cout << "[*] 5. Testing JSON Parser, Editor Save/Load, Undo/Redo & SyncObstacles...\n";

    // JSON edge cases
    surreal::json::JsonValue parsed;
    const std::string jSample = R"({
        "name": "Quoted \"Prop\"\nUTF8: \u00E4",
        "nums": [-12.5, 0, 42],
        "flag": true,
        "nil": null
    })";
    CHECK(surreal::json::Parse(jSample, parsed) && parsed.IsObject(), "JSON parser parses escapes, unicode, arrays, null");
    CHECK(parsed.GetBool("flag", false) == true && parsed.Find("nil")->IsNull(), "JSON preserves bool and null");
    CHECK(parsed.GetString("name").find("Quoted \"Prop\"") != std::string::npos, "JSON decodes escaped quotes");
    surreal::json::JsonValue bad;
    CHECK(!surreal::json::Parse("{broken json", bad), "Malformed JSON fails cleanly without crashing");

    // Portable path helper
    CHECK(surreal::paths::ShellEscapeSingle("a'b") == "'a'\\''b'", "ShellEscapeSingle escapes single quotes safely");

    // Editor workflow
    SurrealWorld world;
    std::vector<ObstacleBox> obstacles;
    SurrealEditor editor;

    size_t evictedCount = 0;
    editor.onMeshEvicted = [&](const std::shared_ptr<SurrealMesh>& m) {
        if (m) ++evictedCount;
    };

    editor.SpawnPlatform(world, obstacles);
    CHECK(editor.entities.size() == 1 && obstacles.size() == 1, "SpawnPlatform adds entity and obstacle");

    // Test rotated SyncObstacles footprint expansion
    editor.RotateSelected(0.785398f, world, &obstacles); // 45 deg
    float hxRotated = obstacles[0].aabb.Extents().x;
    CHECK(hxRotated > 1.15f, "SyncObstacles accounts for entity yaw rotation via transformed worldBounds");

    // Duplicate + Delete + Eviction + Undo/Redo
    editor.DuplicateSelected(world, obstacles);
    CHECK(editor.entities.size() == 2 && obstacles.size() == 2, "DuplicateSelected clones entity cleanly");

    editor.DeleteSelected(world, obstacles);
    CHECK(editor.entities.size() == 1 && evictedCount >= 1,
          "DeleteSelected removes entity and triggers GPU mesh eviction hook");

    CHECK(editor.Undo(world, obstacles) && editor.entities.size() == 2 && obstacles.size() == 2,
          "Undo restores deleted entity and its collider");
    CHECK(editor.Redo(world, obstacles) && editor.entities.size() == 1 && obstacles.size() == 1,
          "Redo reapplies deletion");
    editor.Undo(world, obstacles);

    // Save -> Reset -> Load roundtrip with asset_path & preset & yaw preservation
    const std::string levelPath = "/tmp/surreal_test_level.json";
    editor.SelectBuiltinPreset(1); // RAMP
    editor.placementPos = Vec3(5.0f, 0.0f, -4.0f);
    editor.placementYaw = 0.5f;
    editor.ConfirmPlacement(world, obstacles);
    CHECK(editor.SaveLevel(levelPath), "SaveLevel succeeds");

    // Reset non-core entities then reload
    SurrealEditor reloaded;
    SurrealWorld world2;
    std::vector<ObstacleBox> obs2;
    CHECK(reloaded.LoadLevel(levelPath, world2, obs2), "LoadLevel parses v2 JSON cleanly");
    CHECK(reloaded.entities.size() == editor.entities.size() && obs2.size() == editor.entities.size(),
          "LoadLevel restores exact entity count and colliders");
    bool foundRamp = false;
    for (const auto& e : reloaded.entities) {
        if (e.presetType == ModelPresetType::RAMP && std::fabs(e.yaw - 0.5f) < 1e-4f) {
            foundRamp = true;
        }
    }
    CHECK(foundRamp, "LoadLevel preserves presetType (RAMP) and yaw rotation");

    // Legacy v1 backward-compatibility check (missing asset_path/preset/yaw)
    const std::string v1Path = "/tmp/surreal_test_v1.json";
    {
        std::ofstream f(v1Path);
        f << R"({"engine":"AION Surreal 3D Engine","version":1.0,"entities":[{"id":9,"name":"OldBox","type":"PLATFORM","position":[1,2,3],"scale":[2,1,2],"color":[1,0,0],"metallic":0.2,"roughness":0.8}]})";
    }
    SurrealEditor legacyEd;
    SurrealWorld world3;
    std::vector<ObstacleBox> obs3;
    CHECK(legacyEd.LoadLevel(v1Path, world3, obs3) && legacyEd.entities.size() == 1 && obs3.size() == 1,
          "LoadLevel supports legacy v1 JSON cleanly");

    std::remove(levelPath.c_str());
    std::remove(v1Path.c_str());
}

static void TestAionLinkNonBlockingSocket() {
    std::cout << "[*] 6. Testing Non-Blocking AionLink Loopback Handshake & Bounded Buffer...\n";

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(srv >= 0, "Ephemeral TCP server socket created");
    if (srv < 0) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral port
    CHECK(::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0, "Bound loopback socket");
    socklen_t len = sizeof(addr);
    ::getsockname(srv, reinterpret_cast<sockaddr*>(&addr), &len);
    uint16_t port = ntohs(addr.sin_port);
    CHECK(::listen(srv, 1) == 0, "Listening on ephemeral port");

    std::string receivedTelemetry;
    std::thread srvThread([&]() {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        int cfd = ::accept(srv, reinterpret_cast<sockaddr*>(&cli), &clen);
        if (cfd >= 0) {
            const char* msg =
                "{\"event\":\"CONNECTED\",\"generation\":77,\"rules_count\":42}\n"
                "{\"event\":\"NEW_AUTONOMOUS_GOAL\",\"goal\":\"TestSync\"}\n";
            ::send(cfd, msg, std::strlen(msg), 0);
            // Send oversized line without newline (> 70 KB) to verify rx overflow protection
            std::string noise(70000, 'X');
            ::send(cfd, noise.data(), noise.size(), 0);

            char buf[2048] = {};
            usleep(30000);
            ssize_t n = ::recv(cfd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
            if (n > 0) receivedTelemetry.assign(buf, static_cast<size_t>(n));
            ::close(cfd);
        }
    });

    AionLink link;
    link.enabled = true;
    link.unixSocketPath = "none";
    link.tcpHost = "127.0.0.1";
    link.tcpPort = port;

    // Step Update enough to trigger immediate reconnect + non-blocking poll completion
    for (int i = 0; i < 20; ++i) {
        link.Update(1.0f, 60.0f, 16.0f, 10, false, "CPU_AVX2");
        if (link.connected && link.generation == 77) break;
        usleep(10000);
    }

    CHECK(link.connected, "AionLink connects cleanly via non-blocking TCP handshake");
    CHECK(link.generation == 77 && link.rulesCount == 42 && link.lastGoal == "TestSync",
          "AionLink parses incoming AION state packet without blocking");

    srvThread.join();
    ::close(srv);
    CHECK(!receivedTelemetry.empty(), "AionLink emits JSON telemetry back to AION");
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "  SURREAL 3D ENGINE - HEADLESS VERIFICATION SUITE\n";
    std::cout << "====================================================================\n";

    TestMathAndPBR();
    TestCPURasterizerAndMeshlets();
    TestECSPhysics();
    TestGLTFHierarchyAndSkeletalCrossFade();
    TestJsonEditorUndoAndRoundtrip();
    TestAionLinkNonBlockingSocket();

    std::cout << "\n====================================================================\n";
    std::cout << "  RESULTS: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}
