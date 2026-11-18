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
#include "surreal_game_master.hpp"
#include "surreal_aion_link.hpp"
#include "surreal_json.hpp"
#include "surreal_paths.hpp"
#include "surreal_smash_cats.hpp"

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

static bool WriteUncompressedRGBA_TGA(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    if (static_cast<int>(rgba.size()) != w * h * 4) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint8_t hdr[18] = {};
    hdr[2] = 2; // Uncompressed true-color
    hdr[12] = static_cast<uint8_t>(w & 0xFF);
    hdr[13] = static_cast<uint8_t>((w >> 8) & 0xFF);
    hdr[14] = static_cast<uint8_t>(h & 0xFF);
    hdr[15] = static_cast<uint8_t>((h >> 8) & 0xFF);
    hdr[16] = 32; // 32-bit BGRA
    hdr[17] = 0x28; // Top-down origin + 8 alpha bits
    std::fwrite(hdr, 1, 18, f);
    std::vector<uint8_t> bgra(rgba.size());
    for (int i = 0; i < w * h; ++i) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    std::fclose(f);
    return true;
}

static void TestPhotoCostumeOverRealSkeleton() {
    std::cout << "[*] 7. Testing Photo->3D Costume Over Adapted Real Skeleton & Back Color Fidelity...\n";

    auto makeCharTGA = [](const std::string& path, int w, int h, int headHalfW, int torsoHalfW, bool addBlackHalo) {
        std::vector<uint8_t> img(static_cast<size_t>(w * h * 4), 0);
        auto setPx = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            size_t i = static_cast<size_t>(y * w + x) * 4;
            img[i + 0] = r; img[i + 1] = g; img[i + 2] = b; img[i + 3] = a;
        };
        int cx = w / 2;
        // Head: crown hair [4..15], peach face [16..34] inside dark hair sides
        for (int y = 4; y <= 34; ++y) {
            for (int x = cx - headHalfW; x <= cx + headHalfW; ++x) {
                bool isFace = (y >= 15 && y <= 31 && std::abs(x - cx) <= headHalfW * 55 / 100);
                if (isFace) setPx(x, y, 232, 186, 152, 255);
                else setPx(x, y, 38, 24, 18, 255);
            }
        }
        // Neck [35..41]
        for (int y = 35; y <= 41; ++y) {
            for (int x = cx - 8; x <= cx + 8; ++x) setPx(x, y, 230, 182, 148, 255);
        }
        // Bright vivid RED jacket/torso [42..106]
        for (int y = 42; y <= 106; ++y) {
            for (int x = cx - torsoHalfW; x <= cx + torsoHalfW; ++x) setPx(x, y, 215, 28, 28, 255);
        }
        // Separated blue legs + dark boots [107..195]
        for (int y = 107; y <= 195; ++y) {
            bool boot = (y >= 175);
            uint8_t lr = boot ? 50 : 35, lg = boot ? 45 : 75, lb = boot ? 45 : 185;
            for (int x = cx - 22; x <= cx - 6; ++x) setPx(x, y, lr, lg, lb, 255);
            for (int x = cx + 6; x <= cx + 22; ++x) setPx(x, y, lr, lg, lb, 255);
        }
        if (addBlackHalo) {
            // Add a 1-2px dark matte semi-transparent fringe around the entire silhouette boundary!
            std::vector<uint8_t> copy = img;
            for (int y = 1; y + 1 < h; ++y) {
                for (int x = 1; x + 1 < w; ++x) {
                    size_t i = static_cast<size_t>(y * w + x) * 4;
                    if (copy[i + 3] > 0) continue;
                    bool adjFg = copy[((y - 1) * w + x) * 4 + 3] > 200 ||
                                 copy[((y + 1) * w + x) * 4 + 3] > 200 ||
                                 copy[(y * w + x - 1) * 4 + 3] > 200 ||
                                 copy[(y * w + x + 1) * 4 + 3] > 200;
                    if (adjFg) setPx(x, y, 2, 2, 2, 150);
                }
            }
        }
        return WriteUncompressedRGBA_TGA(path, w, h, img);
    };

    std::string slimPath = "/tmp/surreal_test_slim_red.tga";
    std::string widePath = "/tmp/surreal_test_wide_mascot.tga";
    std::string platePath = "/tmp/surreal_test_landscape_plate.tga";

    CHECK(makeCharTGA(slimPath, 160, 200, 14, 24, true), "Write slim red-jacket TGA with dark halo");
    CHECK(makeCharTGA(widePath, 180, 200, 32, 46, false), "Write wide mascot TGA");

    // Solid full-frame rectangular plate (no transparent perimeter) -> should fall back to plate relief!
    {
        std::vector<uint8_t> plate(96 * 64 * 4);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 96; ++x) {
                size_t i = static_cast<size_t>(y * 96 + x) * 4;
                plate[i + 0] = 210; plate[i + 1] = 45; plate[i + 2] = 45; plate[i + 3] = 255;
            }
        }
        CHECK(WriteUncompressedRGBA_TGA(platePath, 96, 64, plate), "Write solid plate TGA");
    }

    std::shared_ptr<SurrealMesh> slimMesh, wideMesh, plateMesh;
    std::shared_ptr<SkeletalMeshInstance> slimSkel, wideSkel, plateSkel;
    std::string slimInfo, wideInfo, plateInfo;
    Image3DOptions opt;

    CHECK(SurrealEditor::BuildPhotoPlacementAsset(slimPath, "SlimRed", opt, 0, slimMesh, slimSkel, &slimInfo),
          "Build slim character costume via BuildPhotoPlacementAsset");
    CHECK(SurrealEditor::BuildPhotoPlacementAsset(widePath, "WideMascot", opt, 1, wideMesh, wideSkel, &wideInfo),
          "Build wide character costume via BuildPhotoPlacementAsset");
    CHECK(SurrealEditor::BuildPhotoPlacementAsset(platePath, "PlateRect", opt, 0, plateMesh, plateSkel, &plateInfo),
          "Build solid plate fallback via BuildPhotoPlacementAsset");

    // 1. Character vs plate routing & real Mixamo skeleton verification
    CHECK(slimMesh != nullptr && !slimMesh->vertices.empty() && !slimMesh->submeshes.empty(), "Slim costume mesh non-empty");
    CHECK(slimSkel != nullptr && slimSkel->joints.size() >= 20, "Real skeleton loaded (>= 20 joints)");
    CHECK(!slimSkel->clips.empty() && slimSkel->HasSkinData(), "Costume skeleton has clips & skinning data");
    CHECK(plateMesh != nullptr && plateSkel == nullptr, "Full-frame solid plate routes to static relief without human rig");

    int jHips = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::HIPS);
    int jHead = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::HEAD);
    int jShL = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::L_SHOULDER);
    int jArmL = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::L_ARM);
    int jArmR = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::R_ARM);
    int jUpLegL = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::L_UPLEG);
    int jUpLegR = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::R_UPLEG);
    CHECK(jHips >= 0 && jHead >= 0 && jShL >= 0 && jArmL >= 0 && jArmR >= 0 && jUpLegL >= 0 && jUpLegR >= 0,
          "Role-based joint lookup resolves Hips, Head, Shoulder, Arms & Legs");

    // 2. Proportional bone adaptation check (wide mascot scales wider than slim character)
    CHECK(wideSkel != nullptr && jHead >= 0 && jHead < static_cast<int>(wideSkel->joints.size()), "Wide skeleton valid");
    float slimHeadScale = slimSkel->joints[jHead].boneLengthScale.x;
    float wideHeadScale = wideSkel->joints[jHead].boneLengthScale.x;
    CHECK(wideHeadScale > slimHeadScale * 1.15f, "Head bone width adapts proportionally to subject width");

    int jSpine2 = SurrealAutoRigger::FindJointByRole(*slimSkel, HumanoidBoneRole::SPINE2);
    if (jSpine2 >= 0) {
        CHECK(wideSkel->joints[jSpine2].boneLengthScale.x > slimSkel->joints[jSpine2].boneLengthScale.x * 1.15f,
              "Upper torso bone width adapts proportionally to subject shoulders");
    }

    // 3. Genuine 3D side volume check in rest pose (not a ~2% flat relief card)
    slimMesh->CalculateBounds();
    Vec3 ext = slimMesh->worldBounds.max - slimMesh->worldBounds.min;
    CHECK(ext.y > 1.5f && ext.z >= 0.13f * ext.y, "3D costumed figure rest pose has true 3D Z side volume (>= 13% height)");

    // 4. Dual-hemisphere Atlas color fidelity (back torso stays vivid red & back skull gets crown hair, zero halo)
    const SubMesh& sub = slimMesh->submeshes[0];
    CHECK(sub.texRGBA != nullptr && sub.texWidth > 0 && sub.texHeight > 0 && (sub.texWidth % 2 == 0),
          "Dual-hemisphere costume texture atlas present");
    if (sub.texRGBA && sub.texWidth > 4 && sub.texHeight > 20) {
        int singleW = sub.texWidth / 2;
        int frontTorsoX = singleW / 2;
        int backTorsoX = singleW + singleW / 2;
        int torsoY = static_cast<int>(sub.texHeight * 0.40f);
        auto sampleAtlas = [&](int x, int y) -> ColorRGB {
            size_t idx = (static_cast<size_t>(y) * sub.texWidth + x) * 4;
            return ColorRGB((*sub.texRGBA)[idx] / 255.0f, (*sub.texRGBA)[idx + 1] / 255.0f, (*sub.texRGBA)[idx + 2] / 255.0f);
        };
        ColorRGB frontTorso = sampleAtlas(frontTorsoX, torsoY);
        ColorRGB backTorso = sampleAtlas(backTorsoX, torsoY);
        CHECK(frontTorso.r > 0.72f && frontTorso.g < 0.25f && frontTorso.b < 0.25f, "Front torso jacket preserves vivid red");
        CHECK(backTorso.r > 0.72f && backTorso.g < 0.25f && backTorso.b < 0.25f, "Back torso jacket matches vivid front red");

        // Check back of face region has dark brown hair (not peach skin 232,186,152!)
        int headFaceY = static_cast<int>(sub.texHeight * 0.11f);
        ColorRGB frontFace = sampleAtlas(frontTorsoX, headFaceY);
        ColorRGB backHead = sampleAtlas(backTorsoX, headFaceY);
        CHECK(frontFace.r > 0.75f && frontFace.g > 0.55f, "Front head keeps facial skin tone");
        CHECK(backHead.r < 0.35f && backHead.g < 0.25f, "Back of skull replaces facial skin with crown hair");

        // Check boundary-adjacent front silhouette has NO pitch-black halo artifact (< 40 RGB on bright garment!)
        bool hasBlackHaloOnRedTorso = false;
        for (int y = static_cast<int>(sub.texHeight * 0.35f); y <= static_cast<int>(sub.texHeight * 0.45f); ++y) {
            for (int x = 0; x < singleW; ++x) {
                ColorRGB c = sampleAtlas(x, y);
                if (c.r < 0.10f && c.g < 0.10f && c.b < 0.10f) { hasBlackHaloOnRedTorso = true; break; }
            }
        }
        CHECK(!hasBlackHaloOnRedTorso, "Perimeter black alpha halo decontaminated around red jacket");
    }

    // 5. Verify animation skinning deforms mesh cleanly with finite grounded vertices & deep 3D side extent
    slimSkel->Update(0.55f, false);
    slimMesh->CalculateBounds();
    Vec3 animExt = slimMesh->worldBounds.max - slimMesh->worldBounds.min;
    bool allFinite = true;
    for (const auto& v : slimMesh->vertices) {
        if (!IsFiniteVec3(v.position) || !IsFiniteVec3(v.normal)) { allFinite = false; break; }
    }
    CHECK(allFinite && animExt.y > 1.0f && animExt.z >= 0.16f * animExt.y,
          "Animated 3D costume skinning yields finite articulated vertices with >= 16% Z depth");

    // 6. Verify static relief fallback preserves bright red on plate backside too!
    if (plateMesh && !plateMesh->submeshes.empty() && plateMesh->submeshes[0].texRGBA) {
        const SubMesh& psub = plateMesh->submeshes[0];
        int pw = psub.texWidth;
        int bx = pw * 3 / 4;
        int by = psub.texHeight / 2;
        size_t idx = (static_cast<size_t>(by) * pw + bx) * 4;
        CHECK((*psub.texRGBA)[idx] > 180 && (*psub.texRGBA)[idx + 1] < 80, "Relief plate backside preserves exact front color");
    }

    std::remove(slimPath.c_str());
    std::remove(widePath.c_str());
    std::remove(platePath.c_str());
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

static void TestStudio3DVertexColorAndCatalog() {
    std::cout << "[*] 7. Testing Studio3D COLOR_0 Vertex Colors & studio3d/ Catalog Scan...\n";

    // Build a synthetic static glTF without textures that stores VEC4 COLOR_0 attributes
    const std::string vcolGltf = "/tmp/surreal_test_vcol.gltf";
    const std::string vcolBin = "/tmp/surreal_test_vcol.bin";
    {
        float verts[9] = {0.f, 0.f, 0.f,  1.f, 0.f, 0.f,  0.f, 1.f, 0.f};
        float cols[12] = {
            0.25f, 0.50f, 0.75f, 1.0f,
            0.80f, 0.20f, 0.10f, 1.0f,
            0.10f, 0.90f, 0.40f, 1.0f
        };
        uint16_t idx[4] = {0, 1, 2, 0};
        std::ofstream bf(vcolBin, std::ios::binary);
        bf.write(reinterpret_cast<const char*>(verts), sizeof(verts));
        bf.write(reinterpret_cast<const char*>(cols), sizeof(cols));
        bf.write(reinterpret_cast<const char*>(idx), sizeof(idx));
        bf.close();

        std::ofstream gf(vcolGltf);
        gf << R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "name": "StudioMesh", "mesh": 0 }],
  "meshes": [{
    "name": "ColorTri",
    "primitives": [{
      "attributes": { "POSITION": 0, "COLOR_0": 1 },
      "indices": 2
    }]
  }],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 84, "byteLength": 6 }
  ],
  "buffers": [{ "uri": "surreal_test_vcol.bin", "byteLength": 92 }]
})";
        gf.close();
    }

    SkeletalMeshInstance skelInst;
    bool loadedVcol = SurrealSkeletalLoader::LoadSkeletalModel(vcolGltf, skelInst);
    CHECK(loadedVcol && skelInst.activeDeformedMesh && skelInst.activeDeformedMesh->vertices.size() == 3,
          "SurrealSkeletalLoader loads static glTF without textures");
    if (loadedVcol && skelInst.activeDeformedMesh && skelInst.activeDeformedMesh->vertices.size() == 3) {
        const auto& v0 = skelInst.activeDeformedMesh->vertices[0];
        const auto& v1 = skelInst.activeDeformedMesh->vertices[1];
        CHECK(std::fabs(v0.color.x - 0.25f) < 1e-4f && std::fabs(v0.color.y - 0.50f) < 1e-4f &&
                  std::fabs(v0.color.z - 0.75f) < 1e-4f && std::fabs(v1.color.x - 0.80f) < 1e-4f,
              "SurrealSkeletalLoader preserves COLOR_0 vertex colors accurately");
    }

    // Verify studio3d/ subfolder discovery & IsDanceModel heuristic
    std::error_code ec;
    const std::string scanRoot = "/tmp/surreal_test_scan_root";
    std::filesystem::create_directories(scanRoot + "/studio3d", ec);
    const std::string fakeStudioGlb = scanRoot + "/studio3d/hero_studio_rigged.glb";
    {
        std::ofstream mf(fakeStudioGlb, std::ios::binary);
        mf << "glTF";
    }
    auto discovered = SurrealModelLoader::ScanFolder(scanRoot);
    bool foundInStudio = false;
    for (const auto& item : discovered) {
        if (item.filename == "hero_studio_rigged.glb" && item.fullPath == fakeStudioGlb) {
            foundInStudio = true;
            break;
        }
    }
    CHECK(foundInStudio, "SurrealModelLoader::ScanFolder discovers .glb files inside studio3d/");
    CHECK(SurrealEditor::IsDanceModel(fakeStudioGlb), "SurrealEditor::IsDanceModel flags studio3d/rigged characters");
    CHECK(!SurrealEditor::IsDanceModel("/tmp/building_prop.glb"), "SurrealEditor::IsDanceModel ignores generic static props");

    std::remove(vcolGltf.c_str());
    std::remove(vcolBin.c_str());
    std::filesystem::remove_all(scanRoot, ec);

    // Verify EXT_texture_webp decoding via ResolveGlTFTextureImage + libwebp.so.7 fallback
    const std::string webpGltf = "/tmp/surreal_test_webp.gltf";
    const std::string webpBin = "/tmp/surreal_test_webp.bin";
    {
        float verts[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
        float uvs[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
        uint16_t idx[4] = { 0, 1, 2, 0 };
        const uint8_t webpBytes[38] = {
            0x52, 0x49, 0x46, 0x46, 0x1e, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
            0x56, 0x50, 0x38, 0x4c, 0x11, 0x00, 0x00, 0x00, 0x2f, 0x01, 0x40, 0x00,
            0x00, 0x07, 0xd0, 0xaf, 0x4a, 0x37, 0xa8, 0xff, 0x81, 0x88, 0xe8, 0x7f,
            0x00, 0x00
        };
        // Byte layout: verts(36) + uvs(24) + idx(8 padded) + webp(38) + 2 pad = 108 bytes
        std::ofstream bf(webpBin, std::ios::binary);
        bf.write(reinterpret_cast<const char*>(verts), sizeof(verts));
        bf.write(reinterpret_cast<const char*>(uvs), sizeof(uvs));
        bf.write(reinterpret_cast<const char*>(idx), sizeof(idx));
        bf.write(reinterpret_cast<const char*>(webpBytes), sizeof(webpBytes));
        uint16_t pad = 0;
        bf.write(reinterpret_cast<const char*>(&pad), sizeof(pad));
        bf.close();

        std::ofstream gf(webpGltf);
        gf << R"({
  "asset": { "version": "2.0" },
  "extensionsUsed": ["EXT_texture_webp"],
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "name": "WebPNode", "mesh": 0 }],
  "materials": [{
    "name": "WebPMat",
    "pbrMetallicRoughness": {
      "baseColorFactor": [1, 1, 1, 1],
      "baseColorTexture": { "index": 0 }
    }
  }],
  "textures": [{
    "extensions": { "EXT_texture_webp": { "source": 0 } }
  }],
  "images": [{
    "bufferView": 3,
    "mimeType": "image/webp"
  }],
  "meshes": [{
    "name": "WebPTri",
    "primitives": [{
      "attributes": { "POSITION": 0, "TEXCOORD_0": 1 },
      "indices": 2,
      "material": 0
    }]
  }],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 68, "byteLength": 38 }
  ],
  "buffers": [{ "uri": "surreal_test_webp.bin", "byteLength": 108 }]
})";
        gf.close();
    }

    SkeletalMeshInstance webpInst;
    bool loadedWebp = SurrealSkeletalLoader::LoadSkeletalModel(webpGltf, webpInst);
    CHECK(loadedWebp && webpInst.activeDeformedMesh && !webpInst.activeDeformedMesh->submeshes.empty(),
          "SurrealSkeletalLoader loads EXT_texture_webp glTF cleanly");
    if (loadedWebp && webpInst.activeDeformedMesh && !webpInst.activeDeformedMesh->submeshes.empty()) {
        const auto& sm = webpInst.activeDeformedMesh->submeshes[0];
        bool okTex = (sm.texWidth == 2 && sm.texHeight == 2 && sm.texRGBA && sm.texRGBA->size() == 16 &&
                      (*sm.texRGBA)[0] == 210 && (*sm.texRGBA)[1] == 95 && (*sm.texRGBA)[2] == 65);
        CHECK(okTex, "SurrealSkeletalLoader decodes EXT_texture_webp RGBA pixels accurately");
    }
    std::remove(webpGltf.c_str());
    std::remove(webpBin.c_str());

    // Verify MEOVV 5-member ordering (Gawon, Anna, Ella, Narin, Sooin) in both AutoRigger and GameMaster HUD
    {
        auto figs = SurrealAutoRigger::SegmentGroupAndRig(surreal::paths::DefaultRigGroupImage(""));
        CHECK(figs.size() == 5 &&
              figs[0].name == "MEOVV_1_Gawon" &&
              figs[1].name == "MEOVV_2_Anna" &&
              figs[2].name == "MEOVV_3_Ella" &&
              figs[3].name == "MEOVV_4_Narin" &&
              figs[4].name == "MEOVV_5_Sooin",
              "SurrealAutoRigger maps MEOVV members in exact left-to-right order (Gawon, Anna, Ella, Narin, Sooin)");

        SurrealGameMaster gm;
        CHECK(gm.meovvHolograms[0].entityName == "MEOVV_1_Gawon" &&
              gm.meovvHolograms[1].entityName == "MEOVV_2_Anna" &&
              gm.meovvHolograms[1].displayName == "ANNA" &&
              gm.meovvHolograms[2].entityName == "MEOVV_3_Ella" &&
              gm.meovvHolograms[2].displayName == "ELLA" &&
              gm.meovvHolograms[3].entityName == "MEOVV_4_Narin" &&
              gm.meovvHolograms[4].entityName == "MEOVV_5_Sooin",
              "SurrealGameMaster hologram labels match Anna (slot 2) and Ella (slot 3)");
    }
}

void TestSmashCatsSurrealEdition() {
    std::cout << "\n[Test 9] Smash Cats 3D: Surreal Edition (GLB Classes, Combat, Dragon Boss, CPU Render)...\n";
    using namespace Surreal;

    constexpr uint32_t W = 320, H = 200;
    SmashCatsGame game(W, H);
    std::vector<uint32_t> fb(W * H, 0xFF050812);
    game.engine.AttachFramebuffer(fb.data(), W * sizeof(uint32_t));
    game.Init(false, true);

    int loadedArch = 0;
    for (int i = 0; i < static_cast<int>(SmashEnemyKind::COUNT); ++i) {
        if (game.archetypes[i].loaded) loadedArch++;
    }
    CHECK(loadedArch == 5, "Smash Cats loads all 5 rigged .glb archetypes (Monk, CyberCat, Lioness, CyberBeast, Dragon)");
    CHECK(game.lives == 3 && game.wave == 1 && game.CountAliveEnemies() == 3 && game.spawnQueue.size() == 2 && game.obstacles.empty(),
          "Smash Cats Wave 1 initializes open arena (no middle blocks), 3 lives, 3 vanguard + 2 queued foes");

    game.SwitchClass(SmashCatClass::CYBER_CAT, true);
    CHECK(game.playerClass == SmashCatClass::CYBER_CAT && game.playerAnim.activeDeformedMesh != nullptr,
          "Switching playable cat to CyberCat preserves valid skinned CPU mesh");
    game.SwitchClass(SmashCatClass::LIONESS, true);
    CHECK(game.playerClass == SmashCatClass::LIONESS && game.playerAnim.activeDeformedMesh != nullptr,
          "Switching playable cat to Lioness preserves valid skinned CPU mesh");
    game.SwitchClass(SmashCatClass::KITTEN_MONK, true);

    // Deterministic melee combat strike in front of player
    game.playerYaw = 0.0f;
    game.enemies[0].pos = game.playerPos + Vec3(0.0f, 0.0f, 1.1f);
    game.enemies[0].hp = 2;
    game.attackCooldown = 0.0f;
    bool didAtk = game.TriggerAttack(true);
    CHECK(didAtk && game.score > 0 && game.kills >= 1 && game.comboCount >= 1,
          "Smash Cats heavy melee strike awards damage, kill count, combo, and score");

    // Verify player can actually lose HP & lives (not invincible!)
    game.invulnTimer = 0.0f;
    game.hp = 2;
    game.HurtPlayer(2, Vec3(0.0f, 0.0f, 1.0f));
    CHECK(game.lives == 2 && game.hp == game.maxHp && game.invulnTimer > 0.0f,
          "HurtPlayer properly deducts lives on fatal hit");

    // Verify Wave 3 has NO Dragon Boss yet, while Wave 10 spawns Endboss Chaos Dragon
    game.SpawnWave(3, true);
    bool dragonAtWave3 = false;
    for (const auto& e : game.enemies) if (e.isBoss) dragonAtWave3 = true;
    CHECK(!dragonAtWave3, "Wave 3 stays tactical (Chaos Dragon Endboss deferred to Wave 10)");

    game.SpawnWave(10, true);
    bool hasDragonBoss = false;
    for (const auto& e : game.enemies) {
        if (e.alive && e.isBoss && e.kind == SmashEnemyKind::DRAGON_BOSS) hasDragonBoss = true;
    }
    CHECK(hasDragonBoss && !game.spawnQueue.empty(),
          "Wave 10 spawns Chaos Dragon Endboss with escort & queued reinforcements");

    CameraInputState camIn;
    game.TriggerJumpOrAcrobatics();
    game.Update(0.016f, Vec3(0.0f, 0.0f, 1.0f), camIn, true);
    game.RenderSceneCPU(0.016f, fb.data(), W, H, 60.0f, "CPU TEST");
    int litPixels = 0;
    for (uint32_t px : fb) if (px != 0xFF050812) litPixels++;
    CHECK(litPixels > 5000, "Smash Cats CPU AVX2 path rasterizes full 3D scene & HUD pixels");
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
    TestPhotoCostumeOverRealSkeleton();
    TestStudio3DVertexColorAndCatalog();
    TestSmashCatsSurrealEdition();

    std::cout << "\n====================================================================\n";
    std::cout << "  RESULTS: " << g_passed << " passed, " << g_failed << " failed\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}
