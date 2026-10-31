/**
 * ============================================================================
 * AION SURREAL 3D ENGINE - 3D GAME STUDIO, LIVE DOJO & HYBRID BACKEND
 * Real-time 3D PBR Dojo with Interactive World Editor, Entity Outliner,
 * Transform Gizmos, Jump Physics, Spring-Arm Camera, and CPU+GPU Hybrid Engine.
 * ============================================================================
 */

#include <SDL2/SDL.h>
#include "surreal_engine.hpp"
#include "surreal_camera.hpp"
#include "surreal_animation.hpp"
#include "surreal_terrain.hpp"
#include "surreal_hud.hpp"
#include "surreal_editor.hpp"
#include "surreal_hybrid_renderer.hpp"
#include "surreal_aion_link.hpp"
#include <iostream>
#include <vector>
#include <chrono>

// List of dancing models & avatars available for fast swapping
static const std::vector<std::pair<std::string, std::string>> g_availableAvatars = {
    {"mia", "/root/coding/MeinOS/avatar/scene.gltf"},
    {"wednesday", "assets/models/wednesday_gangnam_style.glb"},
    {"miku", "assets/models/miku_-_dance_animation.glb"},
    {"stormtrooper", "assets/models/dancing_stormtrooper.glb"},
    {"skeleton", "assets/models/skeleton_chicken_dance_1.glb"},
    {"yua", "assets/models/yua_han_samba_dancing.glb"},
    {"hiphop", "assets/models/hip_hop_dancing.glb"},
    {"ivonne", "assets/models/ivonne_dance.glb"},
    {"moon", "assets/models/moon_dance.glb"},
    {"chicken", "assets/models/614230008_kachujin_g_rosales_chicken_dance.glb"},
    {"spooky", "assets/models/spooky_skeleton_dance_2.glb"},
    {"sample_b", "assets/models/avatarsample_b-dance_graceful.glb"},
    {"dragon", "assets/models/black_dragon_with_idle_animation.glb"},
    {"kitten", "assets/models/kitten_monk.glb"},
    {"mint", "assets/models/mint_swimsuit_animated-_neverness_to_everness.glb"}
};
static int g_currentAvatarIndex = 0;

static bool SetPlayerAvatar(
    const std::string& glbPath,
    Surreal::SurrealEngine& engine,
    Surreal::SkeletalMeshInstance& character,
    Surreal::EntityID avatarEntity,
    Surreal::SurrealEditor& editor)
{
    std::string actualPath = glbPath;
    if (!std::filesystem::exists(actualPath)) {
        actualPath = "/root/coding/Surreal-engine-Game-editor/" + glbPath;
    }
    if (!std::filesystem::exists(actualPath)) {
        actualPath = "/root/coding/MeinOS/surreal_engine/" + glbPath;
    }

    Surreal::SkeletalMeshInstance newChar;
    if (!Surreal::SurrealSkeletalLoader::LoadSkeletalModel(actualPath, newChar)) {
        std::cerr << "[-] Failed to load avatar from: " << actualPath << "\n";
        editor.SetStatus("[-] Failed to load avatar: " + actualPath, 4.0f);
        return false;
    }

    character = std::move(newChar);

    // Auto-calculate humanoid scale to normalize to ~1.80m tall
    character.activeDeformedMesh->CalculateBounds();
    float height = character.activeDeformedMesh->worldBounds.max.y - character.activeDeformedMesh->worldBounds.min.y;
    float scaleFactor = 1.0f;
    if (height > 0.0001f) {
        scaleFactor = 1.80f / height;
    }

    Surreal::TransformComponent& ct = engine.world.transforms[avatarEntity];
    ct.scale = Surreal::Vec3(scaleFactor, scaleFactor, scaleFactor);

    Surreal::MeshRendererComponent& mr = engine.world.meshRenderers[avatarEntity];
    mr.mesh = character.activeDeformedMesh;

    std::string filename = std::filesystem::path(actualPath).filename().string();
    std::string cleanName = Surreal::SurrealEditor::GetCleanName(filename);
    std::string clipName = character.clips.empty() ? "None" : character.clips[0].name;

    char msgBuf[160];
    std::snprintf(msgBuf, sizeof(msgBuf), "[✓] Avatar: %s | Dance: %s", cleanName.c_str(), clipName.c_str());
    editor.SetStatus(msgBuf, 4.0f);
    std::cout << "[✓] Player Avatar Switched to: " << cleanName << " (Height: " << height << " -> Scale: " << scaleFactor << ", Clip: " << clipName << ")\n";
    return true;
}

int main(int argc, char* argv[]) {
    bool headless = false;
    bool startInEditor = false;
    bool startFullscreen = false;
    bool useAionLink = true;
    bool loadCity = true; // Default: Seoul City 3D photorealistic environment
    bool loadParkour = false;
    std::string requestedAvatar = "";
    std::string photoPath = "";   // --photo <image>: build a coloured 3D model from a picture
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fullscreen") startFullscreen = true;
        if (arg == "--no-aion") useAionLink = false;
        if (arg == "--no-city" || arg == "--bare") loadCity = false;
        if (arg == "--seoul" || arg == "--city") loadCity = true;
        if (arg == "--parkour") loadParkour = true;
        if (arg == "--photo" && i + 1 < argc) photoPath = argv[++i];
        else if (arg.rfind("--photo=", 0) == 0) photoPath = arg.substr(8);
        if (arg == "--avatar" && i + 1 < argc) requestedAvatar = argv[++i];
        else if (arg.rfind("--avatar=", 0) == 0) requestedAvatar = arg.substr(9);
        if (arg == "--benchmark-headless") {
            headless = true;
        } else if (arg == "--editor") {
            startInEditor = true;
        }
    }

    std::cout << "=========================================================\n";
    std::cout << "  AION SURREAL 3D ENGINE - 3D GAME STUDIO & LIVE DOJO\n";
    std::cout << "  CPU + GPU Hybrid Sovereign Engine for CosmOS & Linux\n";
    std::cout << "=========================================================\n";

    int winW = 960;
    int winH = 640;

    // Initialize SDL2
    SDL_Window* window = nullptr;
    Surreal::SurrealHybridRenderer hybridRenderer;

    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            std::cerr << "[-] SDL_Init failed: " << SDL_GetError() << "\n";
            headless = true;
        } else {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

            window = SDL_CreateWindow(
                "AION SURREAL 3D STUDIO - Hybrid CPU+GPU Game Editor & Dojo",
                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                winW, winH,
                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
            );
            if (!window) {
                std::cerr << "[-] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
                headless = true;
            } else {
                if (startFullscreen) SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                hybridRenderer.Init(window, winW, winH);
            }
        }
    }

    // 1. Initialize Engine & CPU Framebuffers
    std::vector<uint32_t> framebuffer(winW * winH, 0xFF050812);
    std::vector<uint32_t> hudFramebuffer(winW * winH, 0x00000000);
    Surreal::SurrealEngine engine(winW, winH);
    engine.AttachFramebuffer(framebuffer.data(), winW * sizeof(uint32_t));

    // 2. Build 3D Terrain & Obstacle Parkour Environment
    std::cout << "[*] Constructing 3D Cyberpunk Grid Terrain & Parkour Platforms...\n";

    // 2a. Massive Metropolis Ground Floor (220m x 220m = 48,400 m² total surface area)
    Surreal::EntityID floorEntity = engine.world.CreateEntity("MetropolisGroundFloor");
    Surreal::TransformComponent& ft = engine.world.transforms[floorEntity];
    ft.position = Surreal::Vec3(0, -0.005f, 0); // Foundation level seamlessly beneath street asphalt
    ft.scale = Surreal::Vec3(1.0f, 1.0f, 1.0f);

    Surreal::MeshRendererComponent floorRenderer;
    floorRenderer.mesh = Surreal::SurrealTerrainBuilder::CreateMetropolisGround(220.0f, 70);
    floorRenderer.material.albedo = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
    floorRenderer.material.metallic = 0.15f;
    floorRenderer.material.roughness = 0.65f;
    engine.world.meshRenderers[floorEntity] = floorRenderer;

    // Soft Real-Time Planar Contact Shadow Disc Mesh
    auto shadowDiscMesh = Surreal::SurrealTerrainBuilder::CreateShadowDisc(0.45f, 24);

    // 2b. Optional Obstacle Platforms (only when requested or in bare mode)
    std::vector<Surreal::ObstacleBox> obstacles;
    std::vector<Surreal::EntityID> platformEntities;

    if (loadParkour || !loadCity) {
        obstacles.emplace_back(Surreal::Vec3(-1.8f, 0.25f, -1.8f), Surreal::Vec3(0.9f, 0.25f, 0.9f), "Platform_Low", Surreal::ColorRGB(0.0f, 0.95f, 0.85f));
        obstacles.emplace_back(Surreal::Vec3(0.0f, 0.475f, -3.4f), Surreal::Vec3(1.1f, 0.475f, 1.1f), "Platform_Mid", Surreal::ColorRGB(1.0f, 0.15f, 0.65f));
        obstacles.emplace_back(Surreal::Vec3(2.0f, 0.70f, -1.8f), Surreal::Vec3(0.9f, 0.70f, 0.9f), "Platform_High", Surreal::ColorRGB(1.0f, 0.85f, 0.2f));
        obstacles.emplace_back(Surreal::Vec3(-0.8f, 0.925f, -5.2f), Surreal::Vec3(1.0f, 0.925f, 1.0f), "Platform_Apex", Surreal::ColorRGB(0.3f, 0.65f, 1.0f));

        for (size_t i = 0; i < obstacles.size(); ++i) {
            const auto& obs = obstacles[i];
            Surreal::EntityID platEntity = engine.world.CreateEntity(obs.name);
            platformEntities.push_back(platEntity);

            Surreal::TransformComponent& pt = engine.world.transforms[platEntity];
            pt.position = obs.aabb.Center();

            Surreal::MeshRendererComponent platRenderer;
            platRenderer.mesh = Surreal::SurrealTerrainBuilder::CreateBox(obs.aabb.Extents() * 2.0f, obs.color);
            platRenderer.material.albedo = obs.color;
            platRenderer.material.metallic = 0.35f;
            platRenderer.material.roughness = 0.40f;
            engine.world.meshRenderers[platEntity] = platRenderer;
        }
    }

    // 3. Load 3D Skeletal Avatar Character
    const std::string modelPath = "/root/coding/MeinOS/avatar/scene.gltf";
    std::cout << "[*] Loading 3D Avatar into Live Dojo from " << modelPath << "...\n";

    Surreal::SkeletalMeshInstance character;
    if (!Surreal::SurrealSkeletalLoader::LoadSkeletalModel(modelPath, character)) {
        std::cerr << "[-] Failed to load skeletal avatar!\n";
        return 1;
    }
    std::cout << "[✓] 3D Avatar Loaded: 61 Bones, " << character.bindVertices.size() << " Verts.\n";

    // Register Avatar Entity
    Surreal::EntityID avatarEntity = engine.world.CreateEntity("SurrealAvatarPrime");
    Surreal::TransformComponent& charTransform = engine.world.transforms[avatarEntity];
    charTransform.position = Surreal::Vec3(0, 0, 0);
    charTransform.scale = Surreal::Vec3(0.01f, 0.01f, 0.01f);
    charTransform.rotation = Surreal::Quat::FromAxisAngle(Surreal::Vec3(0, 1.0f, 0), M_PI);

    Surreal::MeshRendererComponent mr;
    mr.mesh = character.activeDeformedMesh;
    mr.material.albedo = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
    mr.material.metallic = 0.08f;
    mr.material.roughness = 0.42f;
    engine.world.meshRenderers[avatarEntity] = mr;

    // Attach AION Cognitive World Mind
    Surreal::AionMindComponent mind;
    mind.personalityId = "AION_HYBRID_DOJO_DIRECTOR";
    mind.currentObjective = "Live 3D Studio, World Editor & Hybrid CPU+GPU Engine";
    engine.world.aionMinds[avatarEntity] = mind;

    // 4. Initialize Interactive World Editor
    Surreal::SurrealEditor editor;

    // Aion Nexus <-> Evolution Engine link (non-blocking, optional)
    Surreal::AionLink aionLink;
    aionLink.enabled = useAionLink && !headless;

    // Register entities in Editor Outliner
    Surreal::EditableEntity floorEd;
    floorEd.id = floorEntity;
    floorEd.name = "MetropolisGroundFloor";
    floorEd.position = ft.position;
    floorEd.scale = Surreal::Vec3(220.0f, 0.1f, 220.0f);
    floorEd.color = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
    floorEd.metallic = 0.15f;
    floorEd.roughness = 0.65f;
    floorEd.isStaticTerrain = true;
    editor.entities.push_back(floorEd);

    Surreal::EditableEntity avatarEd;
    avatarEd.id = avatarEntity;
    avatarEd.name = "SurrealAvatarPrime";
    avatarEd.position = Surreal::Vec3(0, 0, 0);
    avatarEd.scale = Surreal::Vec3(1.0f, 1.0f, 1.0f);
    avatarEd.color = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
    avatarEd.isCharacter = true;
    editor.entities.push_back(avatarEd);

    for (size_t i = 0; i < obstacles.size(); ++i) {
        Surreal::EditableEntity platEd;
        platEd.id = platformEntities[i];
        platEd.name = obstacles[i].name;
        platEd.position = obstacles[i].aabb.Center();
        platEd.scale = obstacles[i].aabb.Extents() * 2.0f;
        platEd.color = obstacles[i].color;
        platEd.metallic = 0.35f;
        platEd.roughness = 0.40f;
        editor.entities.push_back(platEd);
    }

    // Multi-District Seoul Metropolis Environment (Connected Multi-Block Boulevard)
    if (loadCity) {
        std::string cityPath = "assets/models/Seoul_City_PBR.glb";
        if (!std::filesystem::exists(cityPath)) {
            cityPath = "/root/coding/MeinOS/surreal_engine/assets/models/Seoul_City_PBR.glb";
        }
        if (!std::filesystem::exists(cityPath)) {
            cityPath = "/root/Downloads/seoul_city__gameready__render__blender__pbr.glb";
        }
        Surreal::SkeletalMeshInstance cityInst;
        if (Surreal::SurrealSkeletalLoader::LoadSkeletalModel(cityPath, cityInst)) {
            struct BlockDef {
                const char* name;
                Surreal::Vec3 pos;
                float yaw;
            };
            // 6 connected city blocks forming a sprawling commercial boulevard with storefronts on both sides
            const BlockDef cityBlocks[6] = {
                {"Seoul_Downtown_Main",      Surreal::Vec3(0.0f, 0.0f, 0.0f),       0.0f},
                {"Seoul_Avenue_South",      Surreal::Vec3(-9.3f, 0.0f, 9.8f),      (float)M_PI},
                {"Seoul_East_District",     Surreal::Vec3(18.2f, 0.0f, 0.0f),      0.0f},
                {"Seoul_West_District",     Surreal::Vec3(-18.2f, 0.0f, 0.0f),     0.0f},
                {"Seoul_SouthEast_Arcade",  Surreal::Vec3(8.9f, 0.0f, 9.8f),       (float)M_PI},
                {"Seoul_SouthWest_Arcade",  Surreal::Vec3(-27.5f, 0.0f, 9.8f),      (float)M_PI}
            };

            for (const auto& blk : cityBlocks) {
                Surreal::EntityID cityEntity = engine.world.CreateEntity(blk.name);
                Surreal::TransformComponent& ct = engine.world.transforms[cityEntity];
                ct.position = blk.pos;
                ct.scale = Surreal::Vec3(1.0f, 1.0f, 1.0f);
                ct.rotation = Surreal::Quat::FromAxisAngle(Surreal::Vec3(0, 1.0f, 0), blk.yaw);

                Surreal::MeshRendererComponent cmr;
                cmr.mesh = cityInst.activeDeformedMesh;
                cmr.material.albedo = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
                cmr.material.metallic = 0.20f;
                cmr.material.roughness = 0.50f;
                engine.world.meshRenderers[cityEntity] = cmr;

                Surreal::EditableEntity cityEd;
                cityEd.id = cityEntity;
                cityEd.name = blk.name;
                cityEd.position = ct.position;
                cityEd.scale = ct.scale;
                cityEd.color = cmr.material.albedo;
                cityEd.metallic = cmr.material.metallic;
                cityEd.roughness = cmr.material.roughness;
                cityEd.isStaticTerrain = true;
                editor.entities.push_back(cityEd);
            }
            std::cout << "[✓] Multi-District Seoul Metropolis (6 Connected Blocks) Spawned!\n";

            // Perimeter Metropolis Skyline Towers
            const struct TowerDef { const char* name; Surreal::Vec3 pos; Surreal::Vec3 size; Surreal::ColorRGB col; } towers[4] = {
                {"CyberTower_NorthEast", Surreal::Vec3(55.0f, 12.0f, -40.0f), Surreal::Vec3(8.0f, 24.0f, 8.0f), Surreal::ColorRGB(0.12f, 0.45f, 0.65f)},
                {"CyberTower_NorthWest", Surreal::Vec3(-55.0f, 15.0f, -40.0f), Surreal::Vec3(9.0f, 30.0f, 9.0f), Surreal::ColorRGB(0.65f, 0.15f, 0.45f)},
                {"CyberTower_SouthEast", Surreal::Vec3(55.0f, 14.0f, 50.0f), Surreal::Vec3(8.5f, 28.0f, 8.5f), Surreal::ColorRGB(0.15f, 0.65f, 0.55f)},
                {"CyberTower_SouthWest", Surreal::Vec3(-55.0f, 18.0f, 50.0f), Surreal::Vec3(10.0f, 36.0f, 10.0f), Surreal::ColorRGB(0.85f, 0.55f, 0.15f)}
            };
            for (const auto& tow : towers) {
                Surreal::EntityID te = engine.world.CreateEntity(tow.name);
                engine.world.transforms[te].position = tow.pos;
                Surreal::MeshRendererComponent tmr;
                tmr.mesh = Surreal::SurrealTerrainBuilder::CreateBox(tow.size, tow.col);
                tmr.material.albedo = tow.col;
                tmr.material.metallic = 0.65f;
                tmr.material.roughness = 0.25f;
                engine.world.meshRenderers[te] = tmr;

                Surreal::EditableEntity ted;
                ted.id = te; ted.name = tow.name; ted.position = tow.pos; ted.scale = tow.size;
                ted.color = tow.col; ted.metallic = 0.65f; ted.roughness = 0.25f; ted.isStaticTerrain = true;
                editor.entities.push_back(ted);
            }
        }
    }

    // Optional Photo-to-3D model (via --photo <image.png|jpg>), placed next to the avatar
    if (!photoPath.empty()) {
        Surreal::Image3DInfo p3dInfo;
        auto photoMesh = Surreal::SurrealImage3D::BuildFromFile(photoPath, Surreal::Image3DOptions(), &p3dInfo);
        if (photoMesh) {
            std::string photoName = Surreal::SurrealEditor::GetCleanName(std::filesystem::path(photoPath).filename().string()) + "_3D";
            Surreal::EntityID pe = engine.world.CreateEntity(photoName);
            Surreal::TransformComponent& pt = engine.world.transforms[pe];
            pt.position = Surreal::Vec3(1.6f, 0.0f, 0.0f);
            pt.scale = Surreal::Vec3(1.0f, 1.0f, 1.0f);
            Surreal::MeshRendererComponent pmr;
            pmr.mesh = photoMesh;
            pmr.material.albedo = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
            pmr.material.metallic = 0.05f;
            pmr.material.roughness = 0.85f;
            engine.world.meshRenderers[pe] = pmr;

            Surreal::EditableEntity ped;
            ped.id = pe;
            ped.name = photoName;
            ped.position = pt.position;
            ped.scale = pt.scale;
            ped.color = pmr.material.albedo;
            ped.metallic = pmr.material.metallic;
            ped.roughness = pmr.material.roughness;
            editor.entities.push_back(ped);
            std::cout << "[✓] Photo-to-3D: " << photoName << " (" << photoMesh->vertices.size() << " Verts, "
                      << photoMesh->indices.size() / 3 << " Tris)"
                      << (p3dInfo.plateMode ? " [Platte: kein Hintergrund erkannt]" : "") << "\n";
        } else {
            std::cerr << "[-] Photo-to-3D fehlgeschlagen: " << p3dInfo.error << "\n";
        }
    }

    // Apply requested avatar if specified via CLI
    if (!requestedAvatar.empty()) {
        std::string targetPath = requestedAvatar;
        for (size_t a = 0; a < g_availableAvatars.size(); ++a) {
            std::string lowerKey = g_availableAvatars[a].first;
            std::string lowerReq = requestedAvatar;
            for (char& c : lowerReq) c = tolower(c);
            if (lowerKey.find(lowerReq) != std::string::npos) {
                targetPath = g_availableAvatars[a].second;
                g_currentAvatarIndex = static_cast<int>(a);
                break;
            }
        }
        SetPlayerAvatar(targetPath, engine, character, avatarEntity, editor);
    }

    editor.selectedIndex = static_cast<int>(editor.entities.size() - 1);

    // 5. Initialize Camera Controllers
    Surreal::InteractiveCameraController playCamera;
    playCamera.mode = Surreal::CameraMode::ORBIT_THIRD_PERSON;
    playCamera.distance = 2.8f;
    playCamera.targetDistance = 2.8f;
    playCamera.pitch = 0.28f;
    playCamera.yaw = 0.0f;
    playCamera.SetTarget(Surreal::Vec3(0, 1.25f, 0));

    if (startInEditor) {
        editor.ToggleMode(playCamera.GetPosition(), playCamera.pitch, playCamera.yaw);
    }

    // Setup Atmospheric PBR Lighting with 4 Time-of-Day Modes
    struct AtmosphericProfile {
        const char* name;
        Surreal::Vec3 sunDir;
        Surreal::ColorRGB sunColor;
        float sunIntensity;
        Surreal::ColorRGB ambientColor;
        Surreal::ColorRGB zenithColor;
        Surreal::ColorRGB horizonColor;
        Surreal::ColorRGB groundColor;
        bool isNight;
    };

    const AtmosphericProfile skyProfiles[4] = {
        // 0: CYBERPUNK NIGHT - Authentic midnight city, dark asphalt, neon contrast & starry sky
        {"Cyberpunk Nacht (Night)",
         Surreal::Vec3(-0.35f, 0.85f, -0.40f).Normalized(),
         Surreal::ColorRGB(0.25f, 0.38f, 0.65f), 0.15f,
         Surreal::ColorRGB(0.032f, 0.038f, 0.052f),
         Surreal::ColorRGB(0.002f, 0.003f, 0.008f),
         Surreal::ColorRGB(0.008f, 0.014f, 0.028f),
         Surreal::ColorRGB(0.005f, 0.007f, 0.012f),
         true},

        // 1: DAY - Clear Seoul daylight with deep azure zenith
        {"Tag (Daylight)",
         Surreal::Vec3(0.45f, 0.85f, 0.35f).Normalized(),
         Surreal::ColorRGB(1.0f, 0.96f, 0.88f), 1.20f,
         Surreal::ColorRGB(0.28f, 0.30f, 0.35f),
         Surreal::ColorRGB(0.12f, 0.36f, 0.82f),
         Surreal::ColorRGB(0.60f, 0.72f, 0.88f),
         Surreal::ColorRGB(0.15f, 0.16f, 0.18f),
         false},

        // 2: SUNSET - Golden hour & blazing magenta horizon
        {"Sonnenuntergang (Sunset)",
         Surreal::Vec3(0.85f, 0.18f, 0.45f).Normalized(),
         Surreal::ColorRGB(1.35f, 0.50f, 0.18f), 1.15f,
         Surreal::ColorRGB(0.18f, 0.12f, 0.20f),
         Surreal::ColorRGB(0.18f, 0.06f, 0.28f),
         Surreal::ColorRGB(0.85f, 0.35f, 0.14f),
         Surreal::ColorRGB(0.08f, 0.06f, 0.08f),
         false},

        // 3: NEON DAWN - Indigo morning mist & cyberpunk horizon glow
        {"Neon Morgendaemmerung (Dawn)",
         Surreal::Vec3(-0.6f, 0.38f, 0.65f).Normalized(),
         Surreal::ColorRGB(0.85f, 0.50f, 0.75f), 0.75f,
         Surreal::ColorRGB(0.10f, 0.09f, 0.18f),
         Surreal::ColorRGB(0.06f, 0.04f, 0.18f),
         Surreal::ColorRGB(0.45f, 0.18f, 0.38f),
         Surreal::ColorRGB(0.04f, 0.04f, 0.06f),
         false}
    };
    int currentSkyProfile = 0; // Default to immersive Cyberpunk Night!

    auto applySkyProfile = [&](int idx) {
        const auto& p = skyProfiles[idx % 4];
        engine.world.sunLight.direction = p.sunDir;
        engine.world.sunLight.color = p.sunColor;
        engine.world.sunLight.intensity = p.sunIntensity;
        engine.world.ambientLight = p.ambientColor;
    };
    applySkyProfile(currentSkyProfile);

    // 6. Physics State
    Surreal::Vec3 charPos(0, 0, 0);
    Surreal::Vec3 charVel(0, 0, 0);
    bool isGrounded = true;
    constexpr float GRAVITY = -14.8f;
    constexpr float JUMP_VELOCITY = 6.0f;
    constexpr float MOVE_SPEED = 4.0f;

    bool keyForward = false, keyBackward = false, keyLeft = false, keyRight = false;
    bool requestJump = false;

    // Loop & Benchmark State
    bool running = true;
    bool animPaused = false;
    bool isBenchmarkRunning = false;
    int benchmarkFrameCount = 0;
    constexpr int TOTAL_BENCHMARK_FRAMES = 100;
    std::vector<float> benchFpsList;
    std::vector<float> benchSkinTimes;
    std::vector<float> benchRenderTimes;
    Surreal::BenchmarkResults benchResults;

    Surreal::CameraInputState camInput;
    float currentFps = 60.0f;
    float lastSkinningMs = 0.0f;

    auto lastTime = std::chrono::high_resolution_clock::now();
    uint64_t frameCounter = 0;
    uint64_t totalFramesRendered = 0;
    auto fpsMeasureStart = std::chrono::high_resolution_clock::now();

    std::cout << "[+] Surreal 3D Studio Active!\n";
    std::cout << "    [TAB]        : Toggle Play Mode <-> World Editor Mode\n";
    std::cout << "    [F9]         : Toggle CPU AVX2 Bare-Metal <-> GPU OpenGL 4.6\n";
    std::cout << "    [F6] / [F7]  : Save / Load Level JSON\n";
    std::cout << "    [SPACE]      : Character Jump (Play Mode)\n";
    std::cout << "    [WASD]       : Run / Fly Move\n";
    std::cout << "    [N] / [P]    : Select Next / Prev Entity (Edit Mode)\n";
    std::cout << "    [IJKL / UO]  : Transform Move Entity (Edit Mode)\n";
    std::cout << "    [+] / [-]    : Scale Entity (Edit Mode)\n";
    std::cout << "    [C] / [M]    : Cycle Color / Metallic (Edit Mode)\n";
    std::cout << "    [K]          : Spawn New Platform (Edit Mode)\n";
    std::cout << "    [DEL]        : Delete Entity (Edit Mode)\n";

    float totalAppTime = 0.0f;
    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        dt = std::min(dt, 0.05f);
        totalAppTime += dt;

        editor.Update(dt);
        aionLink.Update(dt, currentFps, engine.lastFrameTimeMs, editor.entities.size(),
                        editor.mode == Surreal::EditorMode::EDIT_MODE, hybridRenderer.GetBackendName());

        camInput.mouseDeltaX = 0;
        camInput.mouseDeltaY = 0;
        camInput.scrollDelta = 0;

        if (!headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    running = false;
                } else if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode sym = ev.key.keysym.sym;
                    uint16_t mod = ev.key.keysym.mod;

                    if (sym == SDLK_ESCAPE) {
                        if (benchResults.isCompleted) {
                            benchResults.isCompleted = false;
                        } else if (isBenchmarkRunning) {
                            isBenchmarkRunning = false;
                            editor.SetStatus("[!] Benchmark cancelled.", 2.5f);
                        } else if (editor.showModelCatalog) {
                            editor.showModelCatalog = false;
                            editor.SetStatus("[!] Model catalog closed.");
                        } else if (editor.isPlacingModel) {
                            editor.CancelPlacement();
                        } else if (editor.mode == Surreal::EditorMode::EDIT_MODE) {
                            editor.ToggleMode(playCamera.GetPosition(), playCamera.pitch, playCamera.yaw);
                        } else {
                            running = false;
                        }
                    } else if (sym == SDLK_b) {
                        if (!isBenchmarkRunning) {
                            isBenchmarkRunning = true;
                            benchmarkFrameCount = 0;
                            benchFpsList.clear();
                            benchSkinTimes.clear();
                            benchRenderTimes.clear();
                            benchResults.isCompleted = false;
                            editor.SetStatus("[*] Benchmark Stress Test started (100 Frames)...", 3.0f);
                        } else {
                            isBenchmarkRunning = false;
                            editor.SetStatus("[!] Benchmark cancelled.", 2.0f);
                        }
                    } else if (sym == SDLK_TAB) {
                        editor.ToggleMode(playCamera.GetPosition(), playCamera.pitch, playCamera.yaw);
                    } else if (sym == SDLK_F11 || (sym == SDLK_RETURN && (mod & KMOD_ALT))) {
                        bool isFs = (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
                        if (SDL_SetWindowFullscreen(window, isFs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
                            editor.SetStatus(std::string("[-] Fullscreen failed: ") + SDL_GetError(), 4.0f);
                        } else {
                            editor.SetStatus(isFs ? "[✓] Windowed Mode [F11]" : "[✓] Fullscreen Mode [F11]", 2.5f);
                        }
                    } else if (sym == SDLK_F9) {
                        hybridRenderer.ToggleBackend();
                        editor.SetStatus("Switched Backend: " + hybridRenderer.GetBackendName(), 3.5f);
                    } else if (sym == SDLK_n) {
                        currentSkyProfile = (currentSkyProfile + 1) % 4;
                        applySkyProfile(currentSkyProfile);
                        editor.SetStatus(std::string("[N] Atmosphaere: ") + skyProfiles[currentSkyProfile].name, 3.5f);
                    } else if (sym == SDLK_F6) {
                        editor.SaveLevel("scene_level.json");
                    } else if (sym == SDLK_F7) {
                        editor.LoadLevel("scene_level.json", engine.world, obstacles);
                    }

                    // --- PLAY MODE CONTROLS ---
                    if (editor.mode == Surreal::EditorMode::PLAY_MODE) {
                        switch (sym) {
                            case SDLK_SPACE: requestJump = true; break;
                            case SDLK_w: keyForward = true; break;
                            case SDLK_s: keyBackward = true; break;
                            case SDLK_a: keyLeft = true; break;
                            case SDLK_d: keyRight = true; break;
                            case SDLK_e: {
                                // Interactive Dance Duet: find nearest dancer entity
                                float nearestDist = 999.0f;
                                std::string nearestName;
                                std::string targetPath;
                                for (const auto& ed : editor.entities) {
                                    if (editor.animatedEntities.count(ed.id) > 0) {
                                        float d = (ed.position - charPos).Length();
                                        if (d < nearestDist && d < 3.2f) {
                                            nearestDist = d;
                                            nearestName = ed.name;
                                            std::string lower = ed.name;
                                            for (char& c : lower) c = tolower(c);
                                            for (size_t a = 0; a < g_availableAvatars.size(); ++a) {
                                                std::string key = g_availableAvatars[a].first;
                                                if (lower.find(key) != std::string::npos) {
                                                    targetPath = g_availableAvatars[a].second;
                                                    g_currentAvatarIndex = static_cast<int>(a);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                                if (!targetPath.empty()) {
                                    SetPlayerAvatar(targetPath, engine, character, avatarEntity, editor);
                                    editor.SetStatus("[★ DANCE DUET ★] Synchronisiere Tanz mit " + nearestName + "!", 4.0f);
                                } else {
                                    g_currentAvatarIndex = (g_currentAvatarIndex + 1) % g_availableAvatars.size();
                                    SetPlayerAvatar(g_availableAvatars[g_currentAvatarIndex].second, engine, character, avatarEntity, editor);
                                }
                                break;
                            }
                            case SDLK_q: playCamera.yaw -= 0.12f; break;
                            case SDLK_LEFT: playCamera.yaw -= 0.08f; break;
                            case SDLK_RIGHT: playCamera.yaw += 0.08f; break;
                            case SDLK_UP: playCamera.pitch = std::min(playCamera.maxPitch, playCamera.pitch + 0.06f); break;
                            case SDLK_DOWN: {
                                float h = playCamera.targetPosition.y - playCamera.minFloorY;
                                float effMin = playCamera.minPitch;
                                if (playCamera.distance > 0.05f) {
                                    float ratio = -h / playCamera.distance;
                                    if (ratio >= -1.0f && ratio <= 1.0f) effMin = std::max(playCamera.minPitch, asinf(ratio) + 0.02f);
                                }
                                playCamera.pitch = std::max(effMin, playCamera.pitch - 0.06f);
                                break;
                            }
                            case SDLK_z: playCamera.targetDistance = std::max(playCamera.minDistance, playCamera.targetDistance - 0.35f); break;
                            case SDLK_x: playCamera.targetDistance = std::min(playCamera.maxDistance, playCamera.targetDistance + 0.35f); break;
                            case SDLK_p: animPaused = !animPaused; break;
                            case SDLK_v: {
                                if (mod & KMOD_SHIFT) {
                                    g_currentAvatarIndex = (g_currentAvatarIndex - 1 + g_availableAvatars.size()) % g_availableAvatars.size();
                                } else {
                                    g_currentAvatarIndex = (g_currentAvatarIndex + 1) % g_availableAvatars.size();
                                }
                                SetPlayerAvatar(g_availableAvatars[g_currentAvatarIndex].second, engine, character, avatarEntity, editor);
                                break;
                            }
                            case SDLK_r:
                                charPos = Surreal::Vec3(0, 0, 0);
                                charVel = Surreal::Vec3(0, 0, 0);
                                playCamera.distance = 2.8f;
                                playCamera.targetDistance = 2.8f;
                                playCamera.pitch = 0.28f;
                                playCamera.yaw = 0.0f;
                                break;
                        }
                    }
                    // --- WORLD EDITOR MODE CONTROLS ---
                    else {
                        if (editor.showModelCatalog) {
                            if (mod & KMOD_SHIFT) {
                                int slot = 0;
                                if (sym >= SDLK_1 && sym <= SDLK_8) slot = sym - SDLK_0;
                                else if (sym >= SDLK_KP_1 && sym <= SDLK_KP_8) slot = sym - SDLK_KP_1 + 1;
                                if (slot > 0) {
                                    int idx = editor.catalogPage * Surreal::SurrealEditor::ITEMS_PER_PAGE + (slot - 1);
                                    if (idx >= 0 && idx < (int)editor.activeCatalogList.size()) {
                                        std::string p = editor.activeCatalogList[idx].fullPath;
                                        if (p.find(".glb") != std::string::npos || p.find(".gltf") != std::string::npos) {
                                            SetPlayerAvatar(p, engine, character, avatarEntity, editor);
                                        } else {
                                            editor.SetStatus("[-] Cannot set static .obj mesh as player avatar", 3.0f);
                                        }
                                    }
                                }
                            } else {
                                int slot = 0;
                                if (sym >= SDLK_1 && sym <= SDLK_8) slot = sym - SDLK_0;
                                else if (sym >= SDLK_KP_1 && sym <= SDLK_KP_8) slot = sym - SDLK_KP_1 + 1;
                                else if (sym == SDLK_9 || sym == SDLK_KP_9) slot = 9;
                                else if (sym == SDLK_0 || sym == SDLK_KP_0) slot = 10;
                                else if (sym == SDLK_m) slot = 11;

                                if (slot > 0) {
                                    if (editor.PickCatalogEntry(slot, character.activeDeformedMesh)) {
                                        editor.placementPos = editor.flyCamPos + editor.GetFlyForward() * 3.5f;
                                        editor.placementPos.y = std::max(0.0f, editor.placementPos.y);
                                    }
                                } else if (sym == SDLK_PERIOD || sym == SDLK_PAGEDOWN || sym == SDLK_RIGHT) {
                                    editor.CatalogNextPage();
                                } else if (sym == SDLK_COMMA || sym == SDLK_PAGEUP || sym == SDLK_LEFT) {
                                    editor.CatalogPrevPage();
                                } else if (sym == SDLK_t) {
                                    editor.CycleCategory();
                                } else if (sym == SDLK_v) {
                                    int idx = editor.catalogPage * Surreal::SurrealEditor::ITEMS_PER_PAGE;
                                    if (idx >= 0 && idx < (int)editor.activeCatalogList.size()) {
                                        SetPlayerAvatar(editor.activeCatalogList[idx].fullPath, engine, character, avatarEntity, editor);
                                    }
                                } else if (sym == SDLK_TAB) {
                                    editor.CycleCategory();
                                } else if (sym == SDLK_F5) {
                                    editor.RefreshModelFolder();
                                    editor.SetStatus("[✓] Rescanned 3D Model Folder: " + std::to_string(editor.folderModels.size()) + " files found");
                                } else if (sym == SDLK_ESCAPE) {
                                    editor.showModelCatalog = false;
                                }
                            }
                        } else if (editor.isPlacingModel) {
                            switch (sym) {
                                case SDLK_RETURN:
                                case SDLK_SPACE:
                                    editor.ConfirmPlacement(engine.world, obstacles);
                                    break;
                                case SDLK_ESCAPE:
                                    editor.CancelPlacement();
                                    break;
                                case SDLK_r:
                                    editor.placementYaw += 0.26f;
                                    break;
                                case SDLK_c:
                                    editor.CycleSelectedColor(engine.world, obstacles);
                                    break;
                                case SDLK_PLUS:
                                case SDLK_EQUALS:
                                    editor.placementScale = editor.placementScale * 1.15f;
                                    break;
                                case SDLK_MINUS:
                                    editor.placementScale = editor.placementScale * 0.85f;
                                    break;
                                case SDLK_i:
                                case SDLK_UP:
                                    editor.placementPos.z -= 0.35f;
                                    break;
                                case SDLK_k:
                                case SDLK_DOWN:
                                    editor.placementPos.z += 0.35f;
                                    break;
                                case SDLK_j:
                                case SDLK_LEFT:
                                    editor.placementPos.x -= 0.35f;
                                    break;
                                case SDLK_l:
                                case SDLK_RIGHT:
                                    editor.placementPos.x += 0.35f;
                                    break;
                                case SDLK_u:
                                case SDLK_PAGEUP:
                                    editor.placementPos.y += 0.25f;
                                    break;
                                case SDLK_o:
                                case SDLK_PAGEDOWN:
                                    editor.placementPos.y = std::max(0.0f, editor.placementPos.y - 0.25f);
                                    break;
                            }
                        } else {
                            switch (sym) {
                                case SDLK_l:
                                    editor.ToggleModelCatalog();
                                    break;
                                case SDLK_w: keyForward = true; break;
                                case SDLK_s: keyBackward = true; break;
                                case SDLK_a: keyLeft = true; break;
                                case SDLK_d:
                                    if (mod & KMOD_SHIFT) {
                                        editor.DuplicateSelected(engine.world, obstacles);
                                    } else {
                                        keyRight = true;
                                    }
                                    break;
                                case SDLK_q: editor.flyCamPos.y -= 0.35f; break;
                                case SDLK_e: editor.flyCamPos.y += 0.35f; break;
                                case SDLK_n: editor.SelectNext(); break;
                                case SDLK_p: editor.SelectPrev(); break;
                                case SDLK_1: editor.SelectIndex(0); break;
                                case SDLK_2: editor.SelectIndex(1); break;
                                case SDLK_3: editor.SelectIndex(2); break;
                                case SDLK_4: editor.SelectIndex(3); break;
                                case SDLK_5: editor.SelectIndex(4); break;
                                case SDLK_6: editor.SelectIndex(5); break;
                                case SDLK_7: editor.SelectIndex(6); break;
                                case SDLK_8: editor.SelectIndex(7); break;
                                case SDLK_9: editor.SelectIndex(8); break;

                                // Gizmo Movement
                                case SDLK_i:
                                case SDLK_UP:
                                    editor.MoveSelected(Surreal::Vec3(0, 0, -0.4f), engine.world, obstacles);
                                    break;
                                case SDLK_k:
                                    if (mod & KMOD_SHIFT) {
                                        editor.SpawnPlatform(engine.world, obstacles);
                                    } else {
                                        editor.MoveSelected(Surreal::Vec3(0, 0, 0.4f), engine.world, obstacles);
                                    }
                                    break;
                                case SDLK_j:
                                case SDLK_LEFT:
                                    editor.MoveSelected(Surreal::Vec3(-0.4f, 0, 0), engine.world, obstacles);
                                    break;
                                case SDLK_RIGHT:
                                case SDLK_SEMICOLON:
                                    editor.MoveSelected(Surreal::Vec3(0.4f, 0, 0), engine.world, obstacles);
                                    break;
                                case SDLK_u:
                                    editor.MoveSelected(Surreal::Vec3(0, -0.25f, 0), engine.world, obstacles);
                                    break;
                                case SDLK_o:
                                    editor.MoveSelected(Surreal::Vec3(0, 0.25f, 0), engine.world, obstacles);
                                    break;

                                // Scaling
                                case SDLK_PLUS:
                                case SDLK_EQUALS:
                                    editor.ScaleSelected(1.15f, engine.world, obstacles);
                                    break;
                                case SDLK_MINUS:
                                    editor.ScaleSelected(0.85f, engine.world, obstacles);
                                    break;

                                // Material & Color
                                case SDLK_c:
                                    editor.CycleSelectedColor(engine.world, obstacles);
                                    break;
                                case SDLK_m:
                                    editor.CycleSelectedPBR(engine.world);
                                    break;

                                // Duplicate & Delete
                                case SDLK_DELETE:
                                case SDLK_x:
                                    editor.DeleteSelected(engine.world, obstacles);
                                    break;
                            }
                        }
                    }
                } else if (ev.type == SDL_KEYUP) {
                    switch (ev.key.keysym.sym) {
                        case SDLK_w: keyForward = false; break;
                        case SDLK_s: keyBackward = false; break;
                        case SDLK_a: keyLeft = false; break;
                        case SDLK_d: keyRight = false; break;
                    }
                } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    if (benchResults.isCompleted) {
                        benchResults.isCompleted = false;
                    }
                    if (editor.mode == Surreal::EditorMode::EDIT_MODE) {
                        if (editor.showModelCatalog && ev.button.button == SDL_BUTTON_LEFT) {
                            auto g = editor.GetCatalogGeom(winW, winH);
                            int mx = ev.button.x, my = ev.button.y;
                            bool inside = mx >= g.x && mx <= g.x + g.w && my >= g.y && my <= g.y + g.h;
                            if (!inside) {
                                editor.showModelCatalog = false;
                            } else if (my >= g.y + 28 && my <= g.y + 48) {
                                editor.CycleModelFolder();
                            } else {
                                int slot = 0;
                                if (my >= g.listY && my < g.listY + g.pageCount * 22) {
                                    slot = (my - g.listY) / 22 + 1;
                                } else if (my >= g.navY - 4 && my < g.navY + 14) {
                                    if (mx < g.x + g.w / 2) editor.CatalogPrevPage(); else editor.CatalogNextPage();
                                } else if (my >= g.presetY - 2 && my < g.presetY + 58) {
                                    int r = (my - (g.presetY - 2)) / 20;
                                    slot = (r == 0) ? 9 : (r == 1) ? 10 : 11;
                                } else if (my > g.y + g.h - 24) {
                                    editor.showModelCatalog = false;
                                }
                                if (slot > 0 && editor.PickCatalogEntry(slot, character.activeDeformedMesh)) {
                                    editor.placementPos = editor.flyCamPos + editor.GetFlyForward() * 3.5f;
                                    editor.placementPos.y = std::max(0.0f, editor.placementPos.y);
                                }
                            }
                        } else if (editor.isPlacingModel) {
                            if (ev.button.button == SDL_BUTTON_LEFT) {
                                editor.ConfirmPlacement(engine.world, obstacles);
                            } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                                editor.CancelPlacement();
                            }
                        } else {
                            if (ev.button.button == SDL_BUTTON_LEFT) camInput.isLeftMouseDown = true;
                            if (ev.button.button == SDL_BUTTON_RIGHT) camInput.isRightMouseDown = true;
                        }
                    } else {
                        if (ev.button.button == SDL_BUTTON_LEFT) camInput.isLeftMouseDown = true;
                        if (ev.button.button == SDL_BUTTON_RIGHT) camInput.isRightMouseDown = true;
                    }
                } else if (ev.type == SDL_MOUSEBUTTONUP) {
                    if (ev.button.button == SDL_BUTTON_LEFT) camInput.isLeftMouseDown = false;
                    if (ev.button.button == SDL_BUTTON_RIGHT) camInput.isRightMouseDown = false;
                } else if (ev.type == SDL_MOUSEMOTION) {
                    if (editor.mode == Surreal::EditorMode::EDIT_MODE && editor.isPlacingModel) {
                        Surreal::Vec3 hit = Surreal::SurrealEditor::ScreenToGround(
                            ev.motion.x, ev.motion.y, winW, winH,
                            engine.cameraPos, engine.cameraTarget, engine.cameraUp,
                            engine.cameraFov, obstacles
                        );
                        editor.placementPos = hit;
                    } else if (camInput.isLeftMouseDown || camInput.isRightMouseDown) {
                        camInput.mouseDeltaX += static_cast<float>(ev.motion.xrel);
                        camInput.mouseDeltaY += static_cast<float>(-ev.motion.yrel);
                    }
                } else if (ev.type == SDL_MOUSEWHEEL) {
                    if (editor.mode == Surreal::EditorMode::EDIT_MODE && editor.isPlacingModel) {
                        if (ev.wheel.y > 0) editor.placementScale = editor.placementScale * 1.10f;
                        else if (ev.wheel.y < 0) editor.placementScale = editor.placementScale * 0.90f;
                    } else {
                        camInput.scrollDelta += static_cast<float>(ev.wheel.y);
                    }
                }
            }
        }

        // --- LIVE VIEWPORT RESIZE (window drag, maximize, F11 fullscreen) ---
        if (!headless && window) {
            int dw = 0, dh = 0;
            SDL_GL_GetDrawableSize(window, &dw, &dh);
            if (dw > 0 && dh > 0 && (dw != winW || dh != winH)) {
                winW = dw;
                winH = dh;
                framebuffer.assign(static_cast<size_t>(winW) * winH, 0xFF050812);
                hudFramebuffer.assign(static_cast<size_t>(winW) * winH, 0x00000000);
                engine.Resize(winW, winH);
                engine.AttachFramebuffer(framebuffer.data(), winW * sizeof(uint32_t));
                hybridRenderer.Resize(winW, winH);
                std::cout << "[*] Viewport resized to " << winW << "x" << winH << "\n";
            }
        }

        // --- CAMERA & WORLD LOGIC ---
        if (editor.mode == Surreal::EditorMode::PLAY_MODE) {
            // 1. Character Movement & Jump Physics
            Surreal::Vec3 forwardVec(-sinf(playCamera.yaw), 0, -cosf(playCamera.yaw));
            forwardVec = forwardVec.Normalized();
            Surreal::Vec3 rightVec = forwardVec.Cross(Surreal::Vec3(0, 1.0f, 0)).Normalized();

            Surreal::Vec3 moveDir(0, 0, 0);
            if (keyForward) moveDir = moveDir + forwardVec;
            if (keyBackward) moveDir = moveDir - forwardVec;
            if (keyRight) moveDir = moveDir + rightVec;
            if (keyLeft) moveDir = moveDir - rightVec;

            if (moveDir.LengthSq() > 0.001f) {
                moveDir = moveDir.Normalized();
                float targetYaw = atan2f(moveDir.x, moveDir.z);
                charTransform.rotation = Surreal::Quat::FromAxisAngle(Surreal::Vec3(0, 1.0f, 0), targetYaw);

                Surreal::Vec3 nextPos = charPos + (moveDir * (MOVE_SPEED * dt));
                constexpr float CHAR_RADIUS = 0.22f;

                for (const auto& obs : obstacles) {
                    if (charPos.y < obs.topY - 0.12f) {
                        if (nextPos.x >= obs.aabb.min.x - CHAR_RADIUS && nextPos.x <= obs.aabb.max.x + CHAR_RADIUS &&
                            charPos.z >= obs.aabb.min.z - CHAR_RADIUS && charPos.z <= obs.aabb.max.z + CHAR_RADIUS)
                        {
                            nextPos.x = charPos.x;
                        }
                        if (charPos.x >= obs.aabb.min.x - CHAR_RADIUS && charPos.x <= obs.aabb.max.x + CHAR_RADIUS &&
                            nextPos.z >= obs.aabb.min.z - CHAR_RADIUS && nextPos.z <= obs.aabb.max.z + CHAR_RADIUS)
                        {
                            nextPos.z = charPos.z;
                        }
                    }
                }
                charPos.x = nextPos.x;
                charPos.z = nextPos.z;
            }

            if (requestJump) {
                if (isGrounded) {
                    charVel.y = JUMP_VELOCITY;
                    isGrounded = false;
                }
                requestJump = false;
            }

            if (!isGrounded) {
                charVel.y += GRAVITY * dt;
                charPos.y += charVel.y * dt;
            }

            float groundY = 0.0f;
            for (const auto& obs : obstacles) {
                if (charPos.x >= obs.aabb.min.x - 0.15f && charPos.x <= obs.aabb.max.x + 0.15f &&
                    charPos.z >= obs.aabb.min.z - 0.15f && charPos.z <= obs.aabb.max.z + 0.15f)
                {
                    if (charPos.y >= obs.topY - 0.35f && obs.topY > groundY) {
                        groundY = obs.topY;
                    }
                }
            }

            if (charPos.y <= groundY) {
                charPos.y = groundY;
                charVel.y = 0;
                isGrounded = true;
            } else if (charPos.y > groundY + 0.05f) {
                isGrounded = false;
            }

            charTransform.position = charPos;

            // Update Spring-Arm Play Camera
            playCamera.SetTarget(charPos + Surreal::Vec3(0, 1.25f, 0));

            float floorUnderCam = 0.35f;
            Surreal::Vec3 currentCamPos = playCamera.GetPosition();
            for (const auto& obs : obstacles) {
                if (currentCamPos.x >= obs.aabb.min.x - 0.35f && currentCamPos.x <= obs.aabb.max.x + 0.35f &&
                    currentCamPos.z >= obs.aabb.min.z - 0.35f && currentCamPos.z <= obs.aabb.max.z + 0.35f)
                {
                    floorUnderCam = std::max(floorUnderCam, obs.topY + 0.30f);
                }
            }
            playCamera.minFloorY = floorUnderCam;
            playCamera.Update(dt, camInput);

            engine.cameraPos = playCamera.GetPosition();
            engine.cameraTarget = playCamera.GetTarget();
            engine.cameraUp = playCamera.GetUp();
        } else {
            // --- WORLD EDITOR FREE-FLY CAMERA ---
            if (camInput.isLeftMouseDown || camInput.isRightMouseDown) {
                editor.flyCamYaw -= camInput.mouseDeltaX * 0.005f;
                editor.flyCamPitch = std::clamp(editor.flyCamPitch + camInput.mouseDeltaY * 0.005f, -1.45f, 1.45f);
            }

            Surreal::Vec3 flyFwd = editor.GetFlyForward();
            Surreal::Vec3 flyRight = editor.GetFlyRight();

            if (keyForward) editor.flyCamPos = editor.flyCamPos + (flyFwd * (editor.flyCamSpeed * dt));
            if (keyBackward) editor.flyCamPos = editor.flyCamPos - (flyFwd * (editor.flyCamSpeed * dt));
            if (keyRight) editor.flyCamPos = editor.flyCamPos + (flyRight * (editor.flyCamSpeed * dt));
            if (keyLeft) editor.flyCamPos = editor.flyCamPos - (flyRight * (editor.flyCamSpeed * dt));

            engine.cameraPos = editor.flyCamPos;
            engine.cameraTarget = editor.flyCamPos + flyFwd;
            engine.cameraUp = Surreal::Vec3(0, 1.0f, 0);
        }

        // --- 2. Skeletal Animation Update ---
        if (!animPaused || isBenchmarkRunning) {
            auto tSkinStart = std::chrono::high_resolution_clock::now();
            character.Update(dt);
            editor.UpdateAnimations(dt);
            auto tSkinEnd = std::chrono::high_resolution_clock::now();
            lastSkinningMs = std::chrono::duration<float, std::milli>(tSkinEnd - tSkinStart).count();
        }

        // Procedural subtle breathing & posture sway for Photo-to-3D models
        float breatheWave = std::sin(totalAppTime * 2.2f);
        for (const auto& ed : editor.entities) {
            if (ed.name.find("_3D") != std::string::npos && editor.animatedEntities.count(ed.id) == 0) {
                auto itTr = engine.world.transforms.find(ed.id);
                if (itTr != engine.world.transforms.end()) {
                    itTr->second.scale = ed.scale * (1.0f + 0.012f * breatheWave);
                    itTr->second.position.y = ed.position.y + 0.004f * breatheWave;
                }
            }
        }

        // --- 3. Dual-Backend Rendering Execution ---
        auto tRenderStart = std::chrono::high_resolution_clock::now();

        if (hybridRenderer.backend == Surreal::RenderBackend::GPU_OPENGL && hybridRenderer.isGLInitialized) {
            // --- GPU HARDWARE OPENGL 4.6 PIPELINE ---
            const auto& curSky = skyProfiles[currentSkyProfile % 4];
            hybridRenderer.BeginFrame(curSky.horizonColor);

            std::vector<Surreal::PointLight> activePointLights;
            if (curSky.isNight) {
                // Street lamps along the boulevard (warm sodium & neon)
                activePointLights.push_back({Surreal::Vec3(0.0f, 4.6f, 2.2f), Surreal::ColorRGB(3.2f, 2.4f, 1.4f), 1.0f, 24.0f});
                activePointLights.push_back({Surreal::Vec3(-9.3f, 4.6f, 7.8f), Surreal::ColorRGB(3.0f, 2.3f, 1.4f), 1.0f, 24.0f});
                activePointLights.push_back({Surreal::Vec3(16.5f, 4.6f, 2.2f), Surreal::ColorRGB(3.0f, 2.3f, 1.4f), 1.0f, 24.0f});
                activePointLights.push_back({Surreal::Vec3(-16.5f, 4.6f, 2.2f), Surreal::ColorRGB(1.6f, 2.6f, 3.8f), 1.0f, 22.0f}); // Cyan neon lamp
                activePointLights.push_back({Surreal::Vec3(8.9f, 4.6f, 7.8f), Surreal::ColorRGB(3.4f, 1.5f, 2.8f), 1.0f, 22.0f});  // Magenta arcade neon
                activePointLights.push_back({Surreal::Vec3(-26.5f, 4.6f, 7.8f), Surreal::ColorRGB(3.0f, 2.3f, 1.4f), 1.0f, 24.0f});
                activePointLights.push_back({Surreal::Vec3(4.2f, 6.2f, -3.8f), Surreal::ColorRGB(2.6f, 1.4f, 3.5f), 1.0f, 25.0f}); // Church cross neon
                // Avatar personal flashlight / aura fill
                activePointLights.push_back({Surreal::Vec3(charPos.x, charPos.y + 1.6f, charPos.z + 0.3f), Surreal::ColorRGB(1.4f, 1.6f, 2.0f), 1.0f, 9.5f});
            }

            hybridRenderer.SetGlobalUniforms(
                engine.world.sunLight.direction,
                engine.world.sunLight.color * engine.world.sunLight.intensity,
                engine.world.ambientLight,
                engine.cameraPos,
                curSky.horizonColor,
                totalAppTime,
                activePointLights
            );

            Surreal::Mat4 vpMat = engine.GetProjectionMatrix() * engine.GetViewMatrix();

            // Render Dynamic Celestial Sky Dome (Deep atmosphere, horizon gradient, sun/moon & stars)
            hybridRenderer.RenderSky(
                vpMat.Inversed(),
                curSky.zenithColor,
                curSky.horizonColor,
                curSky.groundColor,
                curSky.sunDir,
                curSky.sunColor * curSky.sunIntensity,
                curSky.isNight
            );

            // Render all scene entities
            for (auto entityID : engine.world.entities) {
                auto itMr = engine.world.meshRenderers.find(entityID);
                auto itTr = engine.world.transforms.find(entityID);
                if (itMr == engine.world.meshRenderers.end() || itTr == engine.world.transforms.end()) continue;

                bool isDynamic = (entityID == avatarEntity) || (editor.animatedEntities.count(entityID) > 0);
                hybridRenderer.RenderMesh(itMr->second.mesh, itTr->second.GetWorldMatrix(), vpMat, itMr->second.material, isDynamic);
            }

            // Real-Time Contact Ground Shadows
            if (shadowDiscMesh) {
                Surreal::PBRMaterial shadowMat;
                shadowMat.albedo = Surreal::ColorRGB(0.01f, 0.01f, 0.02f);
                shadowMat.metallic = 0.0f;
                shadowMat.roughness = 1.0f;

                // Player Shadow
                float jumpH = std::max(0.0f, charPos.y);
                float pScale = std::max(0.35f, 1.0f / (1.0f + jumpH * 0.7f));
                Surreal::Vec3 pOffset = engine.world.sunLight.direction * (0.08f + jumpH * 0.12f);
                Surreal::Mat4 pShadowMat = Surreal::Mat4::Translation(Surreal::Vec3(charPos.x + pOffset.x, 0.003f, charPos.z + pOffset.z)) *
                                           Surreal::Mat4::Scaling(Surreal::Vec3(pScale, 1.0f, pScale));
                hybridRenderer.RenderMesh(shadowDiscMesh, pShadowMat, vpMat, shadowMat, false);

                // NPC and Photo Shadows
                for (const auto& ed : editor.entities) {
                    if (ed.isCharacter || editor.animatedEntities.count(ed.id) > 0 || ed.name.find("_3D") != std::string::npos) {
                        Surreal::Mat4 npcShadow = Surreal::Mat4::Translation(Surreal::Vec3(ed.position.x, 0.003f, ed.position.z)) *
                                                  Surreal::Mat4::Scaling(Surreal::Vec3(1.1f, 1.0f, 1.1f));
                        hybridRenderer.RenderMesh(shadowDiscMesh, npcShadow, vpMat, shadowMat, false);
                    }
                }
            }

            // Draw Live 3D Placement Model Preview (GPU)
            if (editor.isPlacingModel && editor.placementMesh) {
                Surreal::Mat4 previewWorld = Surreal::Mat4::Translation(editor.placementPos) *
                                             Surreal::Mat4::FromQuat(Surreal::Quat::FromAxisAngle(Surreal::Vec3(0, 1.0f, 0), editor.placementYaw)) *
                                             Surreal::Mat4::Scaling(editor.placementScale);
                Surreal::PBRMaterial previewMat;
                previewMat.albedo = editor.placementColor;
                previewMat.metallic = editor.placementMetallic;
                previewMat.roughness = editor.placementRoughness;
                hybridRenderer.RenderMesh(editor.placementMesh, previewWorld, vpMat, previewMat, false);
            }

            // Draw HUD into transparent buffer
            std::fill(hudFramebuffer.begin(), hudFramebuffer.end(), 0x00000000);
            std::string clipName = character.clips.empty() ? "None" : character.clips[0].name;
            int benchProgress = isBenchmarkRunning ? ((benchmarkFrameCount * 100) / TOTAL_BENCHMARK_FRAMES) : 0;

            if (editor.mode == Surreal::EditorMode::PLAY_MODE) {
                Surreal::SurrealHUD::RenderHUD(
                    hudFramebuffer.data(), winW, winH,
                    currentFps, engine.lastFrameTimeMs + lastSkinningMs, lastSkinningMs,
                    engine.renderedMeshlets, engine.culledMeshlets,
                    character.bindVertices.size(), character.activeDeformedMesh->indices.size() / 3,
                    clipName, character.currentTime,
                    playCamera.distance, playCamera.pitch, playCamera.yaw,
                    isBenchmarkRunning, benchProgress,
                    benchResults.isCompleted ? &benchResults : nullptr,
                    aionLink.generation, aionLink.connected
                );
            }

            // 3D Floating Hologram Speech Bubble above Player Avatar
            Surreal::Vec4 headClip = vpMat.TransformVec4(Surreal::Vec4(charPos.x, charPos.y + 2.05f, charPos.z, 1.0f));
            if (headClip.w > 0.1f) {
                float ndcX = headClip.x / headClip.w;
                float ndcY = headClip.y / headClip.w;
                if (ndcX >= -0.95f && ndcX <= 0.95f && ndcY >= -0.95f && ndcY <= 0.95f) {
                    int sx = static_cast<int>((ndcX * 0.5f + 0.5f) * winW);
                    int sy = static_cast<int>((1.0f - (ndcY * 0.5f + 0.5f)) * winH);

                    // Check nearest dancer for duet prompt
                    std::string nearDancer;
                    for (const auto& ed : editor.entities) {
                        if (editor.animatedEntities.count(ed.id) > 0 && (ed.position - charPos).Length() < 2.8f) {
                            nearDancer = ed.name;
                            break;
                        }
                    }

                    if (!nearDancer.empty()) {
                        char duetBuf[128];
                        std::snprintf(duetBuf, sizeof(duetBuf), "[E] Dance Duet mit %s!", nearDancer.c_str());
                        int dw = 260, dh = 24;
                        int dx = std::max(10, std::min((int)winW - dw - 10, sx - dw / 2));
                        int dy = std::max(30, std::min((int)winH - dh - 10, sy - 34));
                        Surreal::SurrealHUD::DrawPanel(hudFramebuffer.data(), winW, winH, dx, dy, dw, dh, 0x0D1B2A, 0.92f);
                        Surreal::SurrealHUD::DrawString(hudFramebuffer.data(), winW, winH, dx + 10, dy + 7, duetBuf, 0xFFFF0077, 1);
                    } else {
                        // Hologram status badge
                        int hw = 230, hh = 26;
                        int hx = std::max(10, std::min((int)winW - hw - 10, sx - hw / 2));
                        int hy = std::max(30, std::min((int)winH - hh - 10, sy - 32));
                        Surreal::SurrealHUD::DrawPanel(hudFramebuffer.data(), winW, winH, hx, hy, hw, hh, 0x050E18, 0.85f);
                        Surreal::SurrealHUD::DrawString(hudFramebuffer.data(), winW, winH, hx + 8, hy + 8, "[AION] Seoul Metropolis", 0xFF00FFFF, 1);
                    }
                }
            }

            editor.RenderEditorHUD(hudFramebuffer.data(), winW, winH, currentFps, engine.lastFrameTimeMs, hybridRenderer.GetBackendName());
            aionLink.RenderHUD(hudFramebuffer.data(), winW, winH);
            hybridRenderer.BlitHUDOverlay(hudFramebuffer.data(), winW, winH);
            hybridRenderer.Present();

        } else {
            // --- CPU BARE-METAL AVX2 SOFTWARE PIPELINE ---
            engine.RenderFrame(dt);

            // Draw Live 3D Placement Model Preview (CPU Software Rasterizer)
            if (editor.isPlacingModel && editor.placementMesh) {
                Surreal::Mat4 previewWorld = Surreal::Mat4::Translation(editor.placementPos) *
                                             Surreal::Mat4::FromQuat(Surreal::Quat::FromAxisAngle(Surreal::Vec3(0, 1.0f, 0), editor.placementYaw)) *
                                             Surreal::Mat4::Scaling(editor.placementScale);
                Surreal::PBRMaterial previewMat;
                previewMat.albedo = editor.placementColor;
                previewMat.metallic = editor.placementMetallic;
                previewMat.roughness = editor.placementRoughness;
                engine.DrawCustomMesh(editor.placementMesh, previewWorld, previewMat);
            }

            std::string clipName = character.clips.empty() ? "None" : character.clips[0].name;
            int benchProgress = isBenchmarkRunning ? ((benchmarkFrameCount * 100) / TOTAL_BENCHMARK_FRAMES) : 0;
            if (editor.mode == Surreal::EditorMode::PLAY_MODE) {
                Surreal::SurrealHUD::RenderHUD(
                    framebuffer.data(), winW, winH,
                    currentFps, engine.lastFrameTimeMs + lastSkinningMs, lastSkinningMs,
                    engine.renderedMeshlets, engine.culledMeshlets,
                    character.bindVertices.size(), character.activeDeformedMesh->indices.size() / 3,
                    clipName, character.currentTime,
                    playCamera.distance, playCamera.pitch, playCamera.yaw,
                    isBenchmarkRunning, benchProgress,
                    benchResults.isCompleted ? &benchResults : nullptr,
                    aionLink.generation, aionLink.connected
                );
            }

            editor.RenderEditorHUD(framebuffer.data(), winW, winH, currentFps, engine.lastFrameTimeMs, hybridRenderer.GetBackendName());

            aionLink.RenderHUD(framebuffer.data(), winW, winH);
            if (!headless && hybridRenderer.isGLInitialized) {
                hybridRenderer.BlitSoftwareFramebuffer(framebuffer.data(), winW, winH);
                hybridRenderer.Present();
            }
        }

        auto tRenderEnd = std::chrono::high_resolution_clock::now();
        float renderMs = std::chrono::duration<float, std::milli>(tRenderEnd - tRenderStart).count();
        engine.lastFrameTimeMs = renderMs;

        // --- BENCHMARK DATA SAMPLING & COMPUTATION ---
        if (isBenchmarkRunning) {
            benchmarkFrameCount++;
            float totalFrameMs = engine.lastFrameTimeMs + lastSkinningMs;
            float instFps = (totalFrameMs > 0.001f) ? (1000.0f / totalFrameMs) : 60.0f;
            benchFpsList.push_back(instFps);
            benchSkinTimes.push_back(lastSkinningMs);
            benchRenderTimes.push_back(engine.lastFrameTimeMs);

            // Orbit camera smoothly during benchmark test
            playCamera.yaw += 0.035f;

            if (benchmarkFrameCount >= TOTAL_BENCHMARK_FRAMES) {
                benchResults.isCompleted = true;
                benchResults.totalFrames = TOTAL_BENCHMARK_FRAMES;

                float minF = 1e9f, maxF = 0.0f, sumF = 0.0f;
                for (float f : benchFpsList) {
                    if (f < minF) minF = f;
                    if (f > maxF) maxF = f;
                    sumF += f;
                }
                benchResults.minFps = minF;
                benchResults.maxFps = maxF;
                benchResults.avgFps = sumF / benchFpsList.size();

                float sumSkin = 0.0f;
                for (float s : benchSkinTimes) sumSkin += s;
                benchResults.avgSkinningMs = sumSkin / benchSkinTimes.size();

                float sumRender = 0.0f;
                for (float r : benchRenderTimes) sumRender += r;
                benchResults.avgRenderMs = sumRender / benchRenderTimes.size();
                benchResults.avgFrameTimeMs = benchResults.avgSkinningMs + benchResults.avgRenderMs;

                uint64_t totalMeshlets = engine.renderedMeshlets + engine.culledMeshlets;
                benchResults.meshletCullingEfficiency = (totalMeshlets > 0) ?
                    (static_cast<float>(engine.culledMeshlets) / totalMeshlets * 100.0f) : 0.0f;

                benchResults.totalVertices = character.bindVertices.size();
                benchResults.totalTriangles = character.activeDeformedMesh->indices.size() / 3;

                isBenchmarkRunning = false;
                editor.SetStatus("[✓] Benchmark Complete! Scorecard displayed.", 6.0f);

                // Report to Aion Nexus: logged by the Evolution Engine, Aion comments out loud
                aionLink.SendBenchmark(benchResults, winW, winH, hybridRenderer.GetBackendName());
                char speech[160];
                std::snprintf(speech, sizeof(speech),
                    "Surreal Engine benchmark complete. Average %.0f frames per second at %d by %d.",
                    benchResults.avgFps, winW, winH);
                aionLink.Speak(speech);
            }
        }

        // Rolling FPS
        frameCounter++;
        totalFramesRendered++;
        auto elapsedFromFpsStart = std::chrono::duration<float>(tRenderEnd - fpsMeasureStart).count();
        if (elapsedFromFpsStart >= 0.5f) {
            currentFps = frameCounter / elapsedFromFpsStart;
            frameCounter = 0;
            fpsMeasureStart = tRenderEnd;
        }

        if (headless && totalFramesRendered >= 30) {
            running = false;
        }
    }

    if (window) {
        SDL_DestroyWindow(window);
    }
    if (!headless) SDL_Quit();

    std::cout << "[+] Surreal 3D Studio gracefully closed.\n";
    return 0;
}
