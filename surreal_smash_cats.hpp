#pragma once
/**
 * ============================================================================
 * SMASH CATS 3D: SURREAL EDITION - CORE ARENA, COMBAT & SKELETAL 3D ENGINE
 *
 * Transforms the classic bare-metal Smash Cats 3D arcade brawler into a true
 * Surreal 3D Engine C++20 game with rigged 3D glTF/GLB cat fighters, Lioness &
 * CyberBeast enemies, Dragon boss waves, PBR arenas, parkour platform physics,
 * 3-hit melee combos, 360-degree aerial somersault kicks, Meteor Ground Slams,
 * and CPU AVX2 + GPU OpenGL 4.6 hybrid rendering.
 * ============================================================================
 */

#include "surreal_engine.hpp"
#include "surreal_hybrid_renderer.hpp"
#include "surreal_animation.hpp"
#include "surreal_terrain.hpp"
#include "surreal_model_loader.hpp"
#include "surreal_camera.hpp"
#include "surreal_hud.hpp"
#include "surreal_paths.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace Surreal {

enum class SmashCatClass : int {
    KITTEN_MONK = 0,
    CYBER_CAT   = 1,
    LIONESS     = 2,
    COUNT       = 3
};

enum class SmashEnemyKind : int {
    RIVAL_KITTEN = 0,
    CYBER_PROWLER = 1,
    LIONESS_HUNTER = 2,
    CYBER_BEAST  = 3,
    DRAGON_BOSS  = 4,
    COUNT        = 5
};

enum class SmashDropKind : int {
    CATNIP_HEAL = 0,
    FURY_ORB    = 1,
    GIANT_ORB   = 2,
    EXTRA_LIFE  = 3
};

enum class SmashSFX : int {
    PUNCH_LIGHT = 0,
    PUNCH_HEAVY,
    HIT_IMPACT,
    JUMP,
    FLIP_KICK,
    GROUND_SLAM,
    PICKUP_ORB,
    PLAYER_HURT,
    WAVE_CLEAR,
    BOSS_SPAWN,
    GAME_OVER
};

struct SmashSkyProfile {
    const char* name;
    Vec3 sunDir;
    ColorRGB sunColor;
    float sunIntensity;
    ColorRGB ambientColor;
    ColorRGB zenithColor;
    ColorRGB horizonColor;
    ColorRGB groundColor;
    bool isNight;
};

struct SmashClassSpec {
    SmashCatClass id;
    const char* name;
    const char* subtitle;
    int archetypeIdx;       // index into loaded skeletal archetypes
    float moveSpeed;
    float jumpVelocity;
    float attackRange;
    int lightDamage;
    int heavyDamage;
    float slamRadius;
    ColorRGB baseTint;
};

struct SmashEnemyArchetypeSpec {
    const char* name;
    const char* assetPath;
    float targetHeight;
    float speed;
    int baseHp;
    float attackRange;
    float radius;
    int scoreValue;
    bool isBoss;
    int damage;
};

inline constexpr SmashClassSpec kSmashClasses[3] = {
    { SmashCatClass::KITTEN_MONK, "KITTEN MONK",  "Combo Striker (idle/run/kick-1/2)", 0, 7.6f, 8.6f, 2.25f, 2, 4, 4.4f, ColorRGB(1.00f, 1.00f, 1.00f) },
    { SmashCatClass::CYBER_CAT,   "CYBER CAT v13","Hyper Dash & Shock Slam",           1, 9.0f, 9.5f, 2.40f, 2, 4, 5.0f, ColorRGB(0.82f, 1.00f, 1.05f) },
    { SmashCatClass::LIONESS,     "AION LIONESS", "Heavy Apex Brawler",                2, 7.1f, 8.4f, 2.65f, 3, 5, 4.8f, ColorRGB(1.05f, 0.96f, 0.88f) }
};

inline constexpr SmashEnemyArchetypeSpec kEnemySpecs[5] = {
    { "Rival Monk",   "assets/models/kitten_monk.glb",                       1.40f, 4.1f, 5,  1.85f, 0.65f, 120,  false, 1 },
    { "Cyber Prowler","assets/models/Aion_Avatar_13_CyberCat.glb",           1.32f, 5.1f, 6,  1.85f, 0.65f, 150,  false, 1 },
    { "Wild Lioness", "assets/models/Aion_Avatar_10_Lioness.glb",            1.28f, 4.6f, 9,  2.15f, 0.80f, 200,  false, 2 },
    { "Cyber Beast",  "assets/models/Aion_Avatar_11_CyberBeast.glb",         1.68f, 3.7f, 15, 2.35f, 0.95f, 320,  false, 2 },
    { "Chaos Dragon", "assets/models/black_dragon_with_idle_animation.glb",  3.35f, 3.2f, 80, 3.70f, 1.85f, 2500, true,  3 }
};

inline constexpr ColorRGB kFurPresets[6] = {
    ColorRGB(1.00f, 1.00f, 1.00f), // 0: Natural / Original PBR
    ColorRGB(0.45f, 1.00f, 1.15f), // 1: Cyber Neon Cyan
    ColorRGB(1.15f, 0.88f, 0.35f), // 2: Golden Solaris
    ColorRGB(1.15f, 0.50f, 0.90f), // 3: MEOVV Hyper Rose
    ColorRGB(1.15f, 0.42f, 0.42f), // 4: Crimson Fury
    ColorRGB(0.50f, 1.15f, 0.62f)  // 5: Emerald Strike
};
inline constexpr const char* kFurPresetNames[6] = {
    "ORIGINAL", "CYBER CYAN", "SOLAR GOLD", "NEON ROSE", "CRIMSON", "EMERALD"
};

struct AnimClipMap {
    int idle = 0;
    int run  = 0;
    int atk1 = 0;
    int atk2 = 0;
};

/**
 * Shared loaded template per skeletal character archetype so GPU mode uploads each
 * mesh VAO/VBO once while each fighter animates an independent bone palette.
 */
struct ModelArchetype {
    bool loaded = false;
    SkeletalMeshInstance tmpl;
    AnimClipMap clips;
    float normScale = 1.0f;
    float yawOffset = 0.0f;
    std::shared_ptr<SurrealMesh> fallbackMesh;

    static int FindClipIndex(const std::vector<AnimationClip>& list, const std::initializer_list<const char*>& needles, int fallback) {
        for (const char* needle : needles) {
            std::string nLow(needle);
            for (size_t i = 0; i < list.size(); ++i) {
                std::string name = list[i].name;
                for (char& c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                if (name.find(nLow) != std::string::npos) return static_cast<int>(i);
            }
        }
        return std::min(fallback, std::max(0, static_cast<int>(list.size()) - 1));
    }

    static float ComputeFacingOffset(const SkeletalMeshInstance& skel) {
        Vec3 headPos(0, 0, 0), tailPos(0, 0, 0), hipsPos(0, 0, 0);
        bool hasHead = false, hasTail = false, hasHips = false;
        for (const auto& j : skel.joints) {
            std::string ln = j.name;
            for (char& c : ln) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            Vec3 wp = j.globalMatrix.TransformPoint(Vec3(0, 0, 0));
            if (!hasHead && (ln.find("nose") != std::string::npos || ln.find("head") != std::string::npos)) {
                headPos = wp;
                hasHead = true;
            }
            if (!hasTail && ln.find("tail") != std::string::npos) {
                tailPos = wp;
                hasTail = true;
            }
            if (!hasHips && (ln.find("hips") != std::string::npos || ln.find("pelvis") != std::string::npos || ln.find("root") != std::string::npos)) {
                hipsPos = wp;
                hasHips = true;
            }
        }
        if (hasHead && (hasTail || hasHips)) {
            Vec3 ref = hasTail ? tailPos : hipsPos;
            float dx = headPos.x - ref.x;
            float dz = headPos.z - ref.z;
            if (dx * dx + dz * dz > 0.0025f) {
                // Local forward angle relative to +Z: subtracting this aligns forward to +Z.
                float modelForward = atan2f(dx, dz);
                return -modelForward;
            }
        }
        return 0.0f;
    }

    bool Load(const std::string& assetRelOrAbs, float targetHeight, const ColorRGB& fallbackColor) {
        std::string resolved = surreal::paths::ResolveAsset(assetRelOrAbs);
        if (std::filesystem::exists(resolved) && SurrealSkeletalLoader::LoadSkeletalModel(resolved, tmpl)) {
            tmpl.Update(0.0f, true);
            if (tmpl.activeDeformedMesh) {
                tmpl.activeDeformedMesh->CalculateBounds();
                float h = tmpl.activeDeformedMesh->worldBounds.max.y - tmpl.activeDeformedMesh->worldBounds.min.y;
                normScale = (h > 0.001f) ? (targetHeight / h) : 1.0f;
            }
            clips.idle = FindClipIndex(tmpl.clips, {"idle", "breath", "scene"}, 0);
            clips.run  = FindClipIndex(tmpl.clips, {"run", "walk", "fast"}, clips.idle);
            clips.atk1 = FindClipIndex(tmpl.clips, {"kick-1", "kick", "attack"}, clips.run);
            clips.atk2 = FindClipIndex(tmpl.clips, {"kick-2", "spin", "heavy"}, clips.atk1);
            yawOffset  = ComputeFacingOffset(tmpl);
            loaded = true;
            return true;
        }
        fallbackMesh = SurrealTerrainBuilder::CreateBox(Vec3(0.75f, targetHeight, 0.75f), fallbackColor);
        normScale = 1.0f;
        yawOffset = 0.0f;
        loaded = false;
        return false;
    }

    void Instantiate(SkeletalMeshInstance& out, std::shared_ptr<SurrealMesh>& outCpuMesh, bool cpuCopyMesh) const {
        if (!loaded) {
            out = SkeletalMeshInstance();
            outCpuMesh = fallbackMesh;
            return;
        }
        out.joints = tmpl.joints;
        out.skinJointNodeIndices = tmpl.skinJointNodeIndices;
        out.skinJointIBMs = tmpl.skinJointIBMs;
        out.skinMatrices = tmpl.skinMatrices;
        out.evalDone.assign(tmpl.joints.size(), 0);
        out.clips = tmpl.clips;
        out.bindVertices = tmpl.bindVertices;
        out.hasFootBaseOffset = tmpl.hasFootBaseOffset;
        out.staticAttributesInitialized = tmpl.staticAttributesInitialized;
        out.footBaseOffset = tmpl.footBaseOffset;
        out.isZUpModel = tmpl.isZUpModel;
        out.rootNormalizeScale = tmpl.rootNormalizeScale;
        out.rootGroundOffset = tmpl.rootGroundOffset;
        out.activeClipIndex = clips.idle;
        out.previousClipIndex = -1;
        out.currentTime = 0.0f;
        out.playbackSpeed = 1.0f;
        out.isLooping = true;
        out.dirtyGPUUpload = true;

        if (cpuCopyMesh && tmpl.activeDeformedMesh) {
            outCpuMesh = std::make_shared<SurrealMesh>(*tmpl.activeDeformedMesh);
            out.activeDeformedMesh = outCpuMesh;
        } else {
            outCpuMesh = tmpl.activeDeformedMesh;
            out.activeDeformedMesh = tmpl.activeDeformedMesh;
        }
    }
};

struct SmashEnemyUnit {
    bool alive = false;
    SmashEnemyKind kind = SmashEnemyKind::RIVAL_KITTEN;
    SkeletalMeshInstance anim;
    std::shared_ptr<SurrealMesh> cpuMesh;
    Vec3 pos = Vec3(0, 0, 0);
    Vec3 vel = Vec3(0, 0, 0);
    float yaw = 0.0f;
    float baseScale = 1.0f;
    float yawOffset = 0.0f;
    float radius = 0.65f;
    int hp = 4;
    int maxHp = 4;
    float hitFlashTimer = 0.0f;
    float windupTimer = 0.0f;
    float attackCooldown = 0.0f;
    float roamPhase = 0.0f;
    float bossStompCooldown = 0.0f;
    ColorRGB tint = ColorRGB(1.0f, 1.0f, 1.0f);
    AnimClipMap clips;
    bool isBoss = false;
};

struct SmashDropOrb {
    bool active = false;
    SmashDropKind kind = SmashDropKind::CATNIP_HEAL;
    Vec3 pos = Vec3(0, 0, 0);
    float age = 0.0f;
    ColorRGB color = ColorRGB(0.2f, 1.0f, 0.4f);
};

struct SmashShockwaveFX {
    bool active = false;
    Vec3 origin = Vec3(0, 0, 0);
    float radius = 0.4f;
    float maxRadius = 5.0f;
    float speed = 13.0f;
    ColorRGB color = ColorRGB(0.0f, 0.95f, 1.0f);
};

struct PendingEnemySpawn {
    SmashEnemyKind kind = SmashEnemyKind::RIVAL_KITTEN;
    float delay = 3.0f;
    ColorRGB tint = ColorRGB(1.0f, 1.0f, 1.0f);
    int slotIdx = 0;
    int totalSlots = 1;
};

/**
 * Complete Smash Cats 3D: Surreal Edition gameplay & scene manager.
 */
class SmashCatsGame {
public:
    static constexpr float ARENA_RADIUS = 25.0f;
    static constexpr float GRAVITY = -22.0f;

    SurrealEngine engine;
    InteractiveCameraController camera;
    std::vector<ObstacleBox> obstacles;

    // Shared static world meshes
    EntityID floorEntity = INVALID_ENTITY;
    std::shared_ptr<SurrealMesh> shadowDiscMesh;
    std::shared_ptr<SurrealMesh> ringMesh;
    std::shared_ptr<SurrealMesh> dropOrbMesh;
    std::shared_ptr<SurrealMesh> pylonMesh;
    std::shared_ptr<SurrealMesh> turretMesh;
    std::vector<EntityID> arenaPropEntities;
    std::vector<EntityID> seoulEntities;
    SkeletalMeshInstance seoulCityModel;
    bool arenaPropsBuilt = false;
    bool seoulLoaded = false;
    bool useSeoulArena = false;

    // 5 Skeletal Character Archetypes (used for both hero classes and enemies)
    std::array<ModelArchetype, static_cast<int>(SmashEnemyKind::COUNT)> archetypes;

    // Player state
    SmashCatClass playerClass = SmashCatClass::KITTEN_MONK;
    SkeletalMeshInstance playerAnim;
    std::shared_ptr<SurrealMesh> playerCpuMesh;
    Vec3 playerPos = Vec3(0.0f, 0.0f, 4.0f);
    Vec3 playerVel = Vec3(0.0f, 0.0f, 0.0f);
    float playerYaw = 0.0f;
    bool isGrounded = true;
    int jumpStage = 0; // 0=ground, 1=jumped, 2=somersaulting, 3=slam-diving
    float flipAngleDeg = 0.0f;
    float slamCooldown = 0.0f;
    float attackTimer = 0.0f;
    float attackCooldown = 0.0f;
    bool attackHeavy = false;
    int attackComboAlt = 0;
    float hitFlashTimer = 0.0f;
    float invulnTimer = 0.0f;

    int hp = 10;
    int maxHp = 10;
    int lives = 3;
    int wave = 1;
    int score = 0;
    int highScore = 0;
    int kills = 0;
    int comboCount = 0;
    float comboTimer = 0.0f;
    float furyTimer = 0.0f;
    float giantTimer = 0.0f;
    float screenShake = 0.0f;
    float waveBannerTimer = 3.0f;
    float waveNextDelay = 0.0f;
    bool isPaused = false;
    bool isGameOver = false;
    bool audioMuted = false;
    int furPresetIdx = 0;
    int skyProfileIdx = 0;
    float elapsedTime = 0.0f;

    std::string statusBanner = "SMASH CATS 3D: SURREAL EDITION";
    float statusBannerTimer = 4.0f;

    std::vector<SmashEnemyUnit> enemies;
    std::vector<PendingEnemySpawn> spawnQueue;
    std::vector<SmashDropOrb> drops;
    std::vector<SmashShockwaveFX> shockwaves;

    std::function<void(SmashSFX)> onSFX;

    const std::array<SmashSkyProfile, 4> skyProfiles = {{
        { "Cyberpunk Night", Vec3(-0.35f, 0.45f, -0.75f).Normalized(), ColorRGB(0.65f, 0.72f, 1.0f), 0.90f,
          ColorRGB(0.11f, 0.13f, 0.22f), ColorRGB(0.02f, 0.03f, 0.09f), ColorRGB(0.18f, 0.08f, 0.28f), ColorRGB(0.03f, 0.03f, 0.05f), true },
        { "Golden Arena Sunset", Vec3(-0.6f, 0.22f, -0.75f).Normalized(), ColorRGB(1.0f, 0.62f, 0.30f), 1.45f,
          ColorRGB(0.16f, 0.11f, 0.12f), ColorRGB(0.12f, 0.16f, 0.35f), ColorRGB(0.95f, 0.45f, 0.22f), ColorRGB(0.08f, 0.05f, 0.06f), false },
        { "Crisp Daylight", Vec3(0.45f, 0.80f, -0.45f).Normalized(), ColorRGB(1.0f, 0.96f, 0.88f), 1.60f,
          ColorRGB(0.24f, 0.26f, 0.30f), ColorRGB(0.18f, 0.48f, 0.85f), ColorRGB(0.65f, 0.80f, 0.94f), ColorRGB(0.12f, 0.13f, 0.14f), false },
        { "Neon Dawn", Vec3(0.60f, 0.38f, 0.65f).Normalized(), ColorRGB(0.85f, 0.55f, 0.85f), 1.10f,
          ColorRGB(0.14f, 0.12f, 0.24f), ColorRGB(0.06f, 0.04f, 0.18f), ColorRGB(0.42f, 0.18f, 0.38f), ColorRGB(0.04f, 0.04f, 0.06f), true }
    }};

    explicit SmashCatsGame(uint32_t width = 1280, uint32_t height = 720)
        : engine(width, height)
    {
        camera.distance = 5.4f;
        camera.targetDistance = 5.4f;
        camera.minDistance = 2.6f;
        camera.maxDistance = 12.5f;
        camera.pitch = 0.38f;
        camera.yaw = 0.0f;
        camera.minFloorY = 0.45f;
    }

    static std::shared_ptr<SurrealMesh> BuildRingMesh(int segments = 40, float innerR = 0.84f, float outerR = 1.0f) {
        auto mesh = std::make_shared<SurrealMesh>();
        const Vec4 col(1.0f, 1.0f, 1.0f, 0.92f);
        for (int i = 0; i <= segments; ++i) {
            float a = static_cast<float>(i) * TWO_PI / static_cast<float>(segments);
            float ca = cosf(a), sa = sinf(a);
            mesh->vertices.emplace_back(Vec3(ca * innerR, 0.0f, sa * innerR), Vec3(0, 1.0f, 0), 0.0f, static_cast<float>(i) / segments, col);
            mesh->vertices.emplace_back(Vec3(ca * outerR, 0.0f, sa * outerR), Vec3(0, 1.0f, 0), 1.0f, static_cast<float>(i) / segments, col);
        }
        for (int i = 0; i < segments; ++i) {
            uint32_t b = static_cast<uint32_t>(i * 2);
            mesh->indices.push_back(b + 0); mesh->indices.push_back(b + 2); mesh->indices.push_back(b + 1);
            mesh->indices.push_back(b + 1); mesh->indices.push_back(b + 2); mesh->indices.push_back(b + 3);
        }
        mesh->BuildClusters();
        return mesh;
    }

    std::string GetScorePath() const {
        const char* home = std::getenv("HOME");
        std::string base = (home && home[0]) ? std::string(home) : "/tmp";
        return base + "/.surreal_smash_cats_highscore";
    }

    void LoadHighScore() {
        std::ifstream f(GetScorePath());
        if (f.is_open()) {
            int val = 0;
            if (f >> val && val >= 0) highScore = val;
        }
    }

    void SaveHighScore() {
        if (score > highScore) highScore = score;
        std::ofstream f(GetScorePath(), std::ios::trunc);
        if (f.is_open()) f << highScore << "\n";
    }

    void PlaySFX(SmashSFX sfx) const {
        if (!audioMuted && onSFX) onSFX(sfx);
    }

    void SetBanner(const std::string& msg, float dur = 2.8f) {
        statusBanner = msg;
        statusBannerTimer = dur;
    }

    void ApplySkyProfile(int idx) {
        skyProfileIdx = ((idx % 4) + 4) % 4;
        const auto& p = skyProfiles[skyProfileIdx];
        engine.world.sunLight.direction = p.sunDir;
        engine.world.sunLight.color = p.sunColor;
        engine.world.sunLight.intensity = p.sunIntensity;
        engine.world.ambientLight = p.ambientColor;
    }

    void BuildArena(bool seoulMode) {
        useSeoulArena = seoulMode;

        // Ground & shared prop/FX meshes (once)
        if (floorEntity == INVALID_ENTITY) {
            floorEntity = engine.world.CreateEntity("SmashArenaFloor");
            engine.world.transforms[floorEntity].position = Vec3(0, -0.005f, 0);
            MeshRendererComponent fmr;
            fmr.mesh = SurrealTerrainBuilder::CreateMetropolisGround(140.0f, 52);
            fmr.material.albedo = ColorRGB(1.0f, 1.0f, 1.0f);
            fmr.material.metallic = 0.16f;
            fmr.material.roughness = 0.62f;
            fmr.material.allowWetPuddles = true;
            engine.world.meshRenderers[floorEntity] = fmr;
        }

        if (!shadowDiscMesh) shadowDiscMesh = SurrealTerrainBuilder::CreateShadowDisc(0.55f, 24);
        if (!ringMesh) ringMesh = BuildRingMesh(44, 0.85f, 1.0f);
        if (!dropOrbMesh) dropOrbMesh = SurrealModelLoader::CreateOrb(0.32f, 12, 18, ColorRGB(1, 1, 1));

        // Build Open Arena Perimeter Landmarks once (no obstructing middle boxes!)
        if (!arenaPropsBuilt) {
            arenaPropsBuilt = true;

            // Load crystal_pylon.obj & scifi_turret.obj for arena perimeter landmarks
            std::string pylonPath = surreal::paths::ResolveAsset("assets/models/crystal_pylon.obj");
            if (std::filesystem::exists(pylonPath)) {
                SurrealModelLoader::LoadOBJ(pylonPath, pylonMesh, ColorRGB(0.2f, 0.95f, 1.0f));
            }
            if (!pylonMesh) pylonMesh = SurrealModelLoader::CreateMonolith(Vec3(1.2f, 4.5f, 1.2f), ColorRGB(0.15f, 0.90f, 1.0f));

            const Vec3 pylonPos[4] = {
                Vec3(-16.0f, 0.0f, -16.0f), Vec3(16.0f, 0.0f, -16.0f),
                Vec3(-16.0f, 0.0f,  16.0f), Vec3(16.0f, 0.0f,  16.0f)
            };
            for (int i = 0; i < 4; ++i) {
                EntityID pid = engine.world.CreateEntity("ArenaPylon_" + std::to_string(i));
                arenaPropEntities.push_back(pid);
                engine.world.transforms[pid].position = pylonPos[i];
                engine.world.transforms[pid].scale = Vec3(1.8f, 2.2f, 1.8f);
                MeshRendererComponent pmr;
                pmr.mesh = pylonMesh;
                pmr.material.albedo = (i % 2 == 0) ? ColorRGB(0.15f, 0.95f, 1.0f) : ColorRGB(1.0f, 0.25f, 0.78f);
                pmr.material.emissive = pmr.material.albedo * 0.65f;
                pmr.material.metallic = 0.6f;
                pmr.material.roughness = 0.22f;
                engine.world.meshRenderers[pid] = pmr;
            }

            // Perimeter ramparts / monoliths
            std::string turretPath = surreal::paths::ResolveAsset("assets/models/scifi_turret.obj");
            if (std::filesystem::exists(turretPath)) {
                SurrealModelLoader::LoadOBJ(turretPath, turretMesh, ColorRGB(0.75f, 0.80f, 0.90f));
            }
            if (turretMesh) {
                const Vec3 tPos[4] = {
                    Vec3(-18.5f, 0.0f, 0.0f), Vec3(18.5f, 0.0f, 0.0f),
                    Vec3(0.0f, 0.0f, -18.5f), Vec3(0.0f, 0.0f, 18.5f)
                };
                for (int i = 0; i < 4; ++i) {
                    EntityID tid = engine.world.CreateEntity("ArenaTurret_" + std::to_string(i));
                    arenaPropEntities.push_back(tid);
                    engine.world.transforms[tid].position = tPos[i];
                    engine.world.transforms[tid].scale = Vec3(1.4f, 1.4f, 1.4f);
                    engine.world.transforms[tid].rotation = Quat::FromAxisAngle(Vec3(0, 1, 0), atan2f(-tPos[i].x, -tPos[i].z));
                    MeshRendererComponent tmr;
                    tmr.mesh = turretMesh;
                    tmr.material.albedo = ColorRGB(0.8f, 0.85f, 0.95f);
                    tmr.material.metallic = 0.7f;
                    tmr.material.roughness = 0.3f;
                    engine.world.meshRenderers[tid] = tmr;
                }
            }
        }

        // Optionally spawn Seoul_City_PBR.glb backdrop blocks on demand
        if (seoulMode && !seoulLoaded) {
            std::string cityPath = surreal::paths::ResolveAsset("assets/models/Seoul_City_PBR.glb");
            if (!std::filesystem::exists(cityPath)) {
                cityPath = surreal::paths::ResolveAsset("seoul_city__gameready__render__blender__pbr.glb");
            }
            if (!std::filesystem::exists(cityPath)) {
                cityPath = surreal::paths::ResolveAsset("assets/models/Seoul_City_Original.glb");
            }
            if (std::filesystem::exists(cityPath) && SurrealSkeletalLoader::LoadSkeletalModel(cityPath, seoulCityModel)) {
                const std::pair<Vec3, float> blocks[4] = {
                    { Vec3(  0.0f, 0.0f, -24.0f), 0.0f },
                    { Vec3(-18.2f, 0.0f, -24.0f), 0.0f },
                    { Vec3( 18.2f, 0.0f, -24.0f), 0.0f },
                    { Vec3(  0.0f, 0.0f,  26.0f), static_cast<float>(M_PI) }
                };
                for (int i = 0; i < 4; ++i) {
                    EntityID cid = engine.world.CreateEntity("SeoulArenaBlock_" + std::to_string(i));
                    seoulEntities.push_back(cid);
                    engine.world.transforms[cid].position = blocks[i].first;
                    engine.world.transforms[cid].rotation = Quat::FromAxisAngle(Vec3(0, 1, 0), blocks[i].second);
                    MeshRendererComponent cmr;
                    cmr.mesh = seoulCityModel.activeDeformedMesh;
                    cmr.material.albedo = ColorRGB(1, 1, 1);
                    cmr.material.metallic = 0.2f;
                    cmr.material.roughness = 0.5f;
                    cmr.material.allowWetPuddles = true;
                    engine.world.meshRenderers[cid] = cmr;
                }
                seoulLoaded = true;
            }
        }
    }

    bool Init(bool loadSeoulOnStart = false, bool cpuMode = false) {
        LoadHighScore();
        ApplySkyProfile(0);
        BuildArena(loadSeoulOnStart);

        const ColorRGB fallbacks[5] = {
            ColorRGB(1.0f, 0.65f, 0.20f),
            ColorRGB(0.0f, 0.90f, 1.00f),
            ColorRGB(0.95f, 0.78f, 0.35f),
            ColorRGB(0.60f, 0.25f, 0.95f),
            ColorRGB(0.95f, 0.20f, 0.15f)
        };
        for (int i = 0; i < static_cast<int>(SmashEnemyKind::COUNT); ++i) {
            archetypes[i].Load(kEnemySpecs[i].assetPath, kEnemySpecs[i].targetHeight, fallbacks[i]);
        }

        SwitchClass(SmashCatClass::KITTEN_MONK, cpuMode);
        ResetRun(cpuMode);
        return true;
    }

    void SwitchClass(SmashCatClass cls, bool cpuMode) {
        playerClass = cls;
        const auto& spec = kSmashClasses[static_cast<int>(cls)];
        archetypes[spec.archetypeIdx].Instantiate(playerAnim, playerCpuMesh, cpuMode);
        playerAnim.PlayClip(archetypes[spec.archetypeIdx].clips.idle, true);
        SetBanner(std::string("CLASS: ") + spec.name + " // " + spec.subtitle, 2.5f);
    }

    bool ToggleArena() {
        bool targetSeoul = !useSeoulArena;
        BuildArena(targetSeoul);
        if (targetSeoul && !seoulLoaded) {
            useSeoulArena = false;
            SetBanner("Seoul_City_PBR.glb not found - staying in Open Cyber Arena", 3.0f);
            return false;
        }
        SetBanner(useSeoulArena ? "ARENA: SEOUL CYBER PLAZA [V]" : "ARENA: OPEN CYBER ARENA [V]", 2.8f);
        return true;
    }

    void ResetRun(bool cpuMode) {
        playerPos = Vec3(0.0f, 0.0f, 3.0f);
        playerVel = Vec3(0.0f, 0.0f, 0.0f);
        playerYaw = static_cast<float>(M_PI);
        isGrounded = true;
        jumpStage = 0;
        flipAngleDeg = 0.0f;
        slamCooldown = 0.0f;
        attackTimer = 0.0f;
        attackCooldown = 0.0f;
        hitFlashTimer = 0.0f;
        invulnTimer = 0.8f;

        hp = maxHp;
        lives = 3;
        wave = 1;
        score = 0;
        kills = 0;
        comboCount = 0;
        comboTimer = 0.0f;
        furyTimer = 0.0f;
        giantTimer = 0.0f;
        screenShake = 0.0f;
        waveNextDelay = 0.0f;
        isPaused = false;
        isGameOver = false;

        drops.clear();
        shockwaves.clear();
        spawnQueue.clear();
        SpawnWave(wave, cpuMode);
        SetBanner("WAVE 1 - WATCH YOUR FLANKS! 3 LIVES", 3.5f);
    }

    void EnsureMeshModeConsistency(bool cpuMode) {
        if (!cpuMode) return;
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        const auto& pArch = archetypes[spec.archetypeIdx];
        if (pArch.loaded && playerAnim.activeDeformedMesh == pArch.tmpl.activeDeformedMesh && pArch.tmpl.activeDeformedMesh) {
            playerCpuMesh = std::make_shared<SurrealMesh>(*pArch.tmpl.activeDeformedMesh);
            playerAnim.activeDeformedMesh = playerCpuMesh;
        }
        for (auto& e : enemies) {
            if (!e.alive) continue;
            const auto& eArch = archetypes[static_cast<int>(e.kind)];
            if (eArch.loaded && e.anim.activeDeformedMesh == eArch.tmpl.activeDeformedMesh && eArch.tmpl.activeDeformedMesh) {
                e.cpuMesh = std::make_shared<SurrealMesh>(*eArch.tmpl.activeDeformedMesh);
                e.anim.activeDeformedMesh = e.cpuMesh;
            }
        }
    }

    void SpawnWave(int waveNum, bool cpuMode) {
        enemies.clear();
        spawnQueue.clear();
        const ColorRGB eTints[5] = {
            ColorRGB(1.15f, 0.55f, 0.35f),
            ColorRGB(1.00f, 0.38f, 0.70f),
            ColorRGB(1.05f, 0.65f, 0.30f),
            ColorRGB(0.85f, 0.45f, 1.15f),
            ColorRGB(1.10f, 0.35f, 0.30f)
        };

        auto pickKind = [&](int idx, int batch) -> SmashEnemyKind {
            if (waveNum == 1) {
                return (batch == 0) ? SmashEnemyKind::RIVAL_KITTEN : SmashEnemyKind::CYBER_PROWLER;
            }
            if (waveNum == 2) {
                return ((idx + batch) % 2 == 0) ? SmashEnemyKind::RIVAL_KITTEN : SmashEnemyKind::CYBER_PROWLER;
            }
            if (waveNum == 3) {
                if ((idx + batch) % 3 == 0) return SmashEnemyKind::LIONESS_HUNTER;
                return (idx % 2 == 0) ? SmashEnemyKind::CYBER_PROWLER : SmashEnemyKind::RIVAL_KITTEN;
            }
            if (waveNum == 4) {
                if (batch == 2 && idx == 0) return SmashEnemyKind::CYBER_BEAST;
                return (idx % 2 == 0) ? SmashEnemyKind::LIONESS_HUNTER : SmashEnemyKind::CYBER_PROWLER;
            }
            if (waveNum == 5) { // Mini-Boss Wave 5: Alpha Cyber Beast Duo
                if ((batch == 0 || batch == 2) && idx == 0) return SmashEnemyKind::CYBER_BEAST;
                return (batch == 1) ? SmashEnemyKind::LIONESS_HUNTER : SmashEnemyKind::CYBER_PROWLER;
            }
            // Waves 6-9 & endless escalation
            if (idx % 4 == 3 || (batch == 2 && idx == 0)) return SmashEnemyKind::CYBER_BEAST;
            if (idx % 3 == 1) return SmashEnemyKind::LIONESS_HUNTER;
            return (idx % 2 == 0) ? SmashEnemyKind::CYBER_PROWLER : SmashEnemyKind::RIVAL_KITTEN;
        };

        // Endboss every 10th wave starting Wave 10 (10, 15, 20, ...)
        const bool isDragonBossWave = (waveNum >= 10 && (waveNum % 5 == 0));
        if (isDragonBossWave) {
            enemies.push_back(CreateEnemyUnit(SmashEnemyKind::DRAGON_BOSS, 0, 1, waveNum, ColorRGB(1.05f, 0.85f, 0.85f), cpuMode));
            const int escortInit = 2;
            for (int i = 0; i < escortInit; ++i) {
                SmashEnemyKind ek = (i == 0) ? SmashEnemyKind::CYBER_BEAST : SmashEnemyKind::LIONESS_HUNTER;
                enemies.push_back(CreateEnemyUnit(ek, i + 1, escortInit + 1, waveNum, eTints[i % 5], cpuMode));
            }
            // Reinforcement waves during the Dragon Boss battle
            for (int i = 0; i < 3; ++i) {
                SmashEnemyKind ek = (i == 0) ? SmashEnemyKind::LIONESS_HUNTER : SmashEnemyKind::CYBER_PROWLER;
                spawnQueue.push_back({ ek, 6.5f, eTints[(i + 1) % 5], i, 3 });
            }
            for (int i = 0; i < 3; ++i) {
                SmashEnemyKind ek = (i == 0) ? SmashEnemyKind::CYBER_BEAST : SmashEnemyKind::CYBER_PROWLER;
                spawnQueue.push_back({ ek, 13.0f, eTints[(i + 2) % 5], i, 3 });
            }
            PlaySFX(SmashSFX::BOSS_SPAWN);
            waveBannerTimer = 4.0f;
            SetBanner("ENDBOSS WAVE " + std::to_string(waveNum) + ": CHAOS DRAGON DESCENDS!", 4.5f);
            return;
        }

        // Progressive Multi-Burst Wave Composition (Vanguard + 1-2 Staggered Reinforcements)
        int vanguardCount = std::min(5, 3 + waveNum / 3);
        int burst2Count   = std::min(5, 2 + (waveNum - 1) / 2);
        int burst3Count   = (waveNum >= 4) ? std::min(6, 1 + (waveNum - 3)) : 0;

        for (int i = 0; i < vanguardCount; ++i) {
            enemies.push_back(CreateEnemyUnit(pickKind(i, 0), i, vanguardCount, waveNum, eTints[i % 5], cpuMode));
        }
        for (int i = 0; i < burst2Count; ++i) {
            spawnQueue.push_back({ pickKind(i, 1), 3.8f + i * 0.25f, eTints[(i + 2) % 5], i, burst2Count });
        }
        for (int i = 0; i < burst3Count; ++i) {
            spawnQueue.push_back({ pickKind(i, 2), 8.2f + i * 0.25f, eTints[(i + 4) % 5], i, burst3Count });
        }

        if (waveNum == 5) {
            PlaySFX(SmashSFX::BOSS_SPAWN);
            waveBannerTimer = 3.5f;
            SetBanner("WAVE 5 MINI-BOSS: ALPHA CYBER BEAST DUO!", 4.0f);
        } else {
            PlaySFX(SmashSFX::WAVE_CLEAR);
            waveBannerTimer = 2.6f;
            int totalWaveFoes = vanguardCount + burst2Count + burst3Count;
            SetBanner("WAVE " + std::to_string(waveNum) + " (" + std::to_string(totalWaveFoes) + " FOES INCOMING!)", 3.0f);
        }
    }

    bool TriggerJumpOrAcrobatics() {
        if (isPaused || isGameOver) return false;
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        if (isGrounded) {
            playerVel.y = spec.jumpVelocity;
            isGrounded = false;
            jumpStage = 1;
            flipAngleDeg = 0.0f;
            SpawnShockwave(Vec3(playerPos.x, playerPos.y + 0.04f, playerPos.z), 1.8f, 8.0f, ColorRGB(0.0f, 0.95f, 0.85f));
            PlaySFX(SmashSFX::JUMP);
            return true;
        }
        if (jumpStage == 1) {
            jumpStage = 2; // 360 Somersault Kick
            flipAngleDeg = 1.0f;
            playerVel.y = std::max(playerVel.y, spec.jumpVelocity * 0.65f);
            ApplyRadialAttack(playerPos, spec.attackRange + 0.9f, 1 + static_cast<int>(playerClass), 4.0f);
            PlaySFX(SmashSFX::FLIP_KICK);
            return true;
        }
        if (jumpStage == 2 && slamCooldown <= 0.0f) {
            jumpStage = 3; // Meteor Ground Slam dive!
            playerVel.y = -22.0f;
            PlaySFX(SmashSFX::PUNCH_HEAVY);
            return true;
        }
        return false;
    }

    bool TriggerGroundSlamDirect() {
        if (isPaused || isGameOver || isGrounded || jumpStage == 3 || slamCooldown > 0.0f) return false;
        jumpStage = 3;
        playerVel.y = -22.0f;
        PlaySFX(SmashSFX::PUNCH_HEAVY);
        return true;
    }

    bool TriggerAttack(bool heavy) {
        if (isPaused || isGameOver || attackCooldown > 0.0f) return false;
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        const auto& arch = archetypes[spec.archetypeIdx];

        attackHeavy = heavy;
        attackTimer = heavy ? 0.36f : 0.24f;
        attackCooldown = heavy ? 0.42f : 0.22f;
        attackComboAlt = (attackComboAlt + 1) % 2;

        int clipIdx = (heavy || attackComboAlt == 1) ? arch.clips.atk2 : arch.clips.atk1;
        if (playerAnim.clips.size() > 1) {
            playerAnim.PlayClip(clipIdx, false);
        }

        // Lunge slightly forward along player facing direction
        Vec3 fwd(sinf(playerYaw), 0.0f, cosf(playerYaw));
        playerPos = playerPos + fwd * (heavy ? 0.35f : 0.20f);

        int dmg = heavy ? spec.heavyDamage : spec.lightDamage;
        if (furyTimer > 0.0f) dmg *= 2;
        if (giantTimer > 0.0f) dmg += 1;

        float reach = spec.attackRange * (giantTimer > 0.0f ? 1.30f : 1.0f) + (heavy ? 0.45f : 0.0f);
        float knock = heavy ? 5.6f : 2.4f;

        PlaySFX(heavy ? SmashSFX::PUNCH_HEAVY : SmashSFX::PUNCH_LIGHT);
        int hits = ApplyMeleeConeAttack(playerPos, fwd, reach, heavy ? -0.05f : 0.15f, dmg, knock);
        if (hits > 0 && heavy) {
            SpawnShockwave(Vec3(playerPos.x + fwd.x * 1.1f, playerPos.y + 0.08f, playerPos.z + fwd.z * 1.1f),
                           2.6f, 12.0f, ColorRGB(1.0f, 0.35f, 0.80f));
        }
        return true;
    }

    void Update(float dt, const Vec3& wishMoveWorld, const CameraInputState& camIn, bool cpuMode) {
        elapsedTime += dt;
        if (statusBannerTimer > 0.0f) statusBannerTimer = std::max(0.0f, statusBannerTimer - dt);
        if (waveBannerTimer > 0.0f) waveBannerTimer = std::max(0.0f, waveBannerTimer - dt);

        if (isPaused || isGameOver) {
            camera.SetTarget(playerPos + Vec3(0, 1.15f, 0));
            camera.Update(dt, camIn);
            UpdateEngineCamera(dt);
            return;
        }

        if (hitFlashTimer > 0.0f) hitFlashTimer = std::max(0.0f, hitFlashTimer - dt);
        if (invulnTimer > 0.0f) invulnTimer = std::max(0.0f, invulnTimer - dt);
        if (slamCooldown > 0.0f) slamCooldown = std::max(0.0f, slamCooldown - dt);
        if (attackCooldown > 0.0f) attackCooldown = std::max(0.0f, attackCooldown - dt);
        if (attackTimer > 0.0f) attackTimer = std::max(0.0f, attackTimer - dt);
        if (furyTimer > 0.0f) furyTimer = std::max(0.0f, furyTimer - dt);
        if (giantTimer > 0.0f) giantTimer = std::max(0.0f, giantTimer - dt);
        if (screenShake > 0.0f) screenShake = std::max(0.0f, screenShake - dt * 3.0f);
        if (comboTimer > 0.0f) {
            comboTimer -= dt;
            if (comboTimer <= 0.0f) comboCount = 0;
        }

        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        const float charHeight = (giantTimer > 0.0f) ? 2.0f : 1.45f;
        const float charRadius = (giantTimer > 0.0f) ? 0.55f : 0.42f;
        float speedMult = spec.moveSpeed * (furyTimer > 0.0f ? 1.18f : 1.0f);

        // Horizontal movement with obstacle AABB collision & arena circular clamping
        bool moving = wishMoveWorld.LengthSq() > 0.001f;
        if (moving) {
            Vec3 dir = wishMoveWorld.Normalized();
            float targetYaw = atan2f(dir.x, dir.z);
            float dy = targetYaw - playerYaw;
            while (dy > M_PI) dy -= TWO_PI;
            while (dy < -M_PI) dy += TWO_PI;
            playerYaw += dy * std::min(1.0f, dt * 16.0f);

            Vec3 nextPos = playerPos + dir * (speedMult * dt);
            ResolveObstacleHorizontal(playerPos, nextPos, charRadius, charHeight);
            playerPos.x = nextPos.x;
            playerPos.z = nextPos.z;
        }
        ClampInsideArena(playerPos, ARENA_RADIUS - 0.6f);

        // Vertical gravity, jump, somersault AoE & ground slam landing
        if (!isGrounded) {
            playerVel.y += GRAVITY * dt;
            playerPos.y += playerVel.y * dt;
        }
        if (jumpStage == 2) {
            flipAngleDeg += dt * 780.0f;
            if (flipAngleDeg >= 360.0f) flipAngleDeg = 0.0f;
            // Sustained somersault spin deals controlled AoE damage without stunlocking crowds
            if (fmodf(elapsedTime, 0.26f) < dt) {
                ApplyRadialAttack(playerPos, spec.attackRange + 0.35f, 1, 2.0f);
            }
        }

        float groundY = ComputeSupportGroundY(playerPos.x, playerPos.z, playerPos.y, charRadius * 0.7f);
        if (playerPos.y <= groundY) {
            if (jumpStage == 3) {
                OnGroundSlamImpact(groundY);
            }
            playerPos.y = groundY;
            playerVel.y = 0.0f;
            isGrounded = true;
            jumpStage = 0;
            flipAngleDeg = 0.0f;
        } else if (playerPos.y > groundY + 0.06f) {
            isGrounded = false;
        }

        // Animate player rig cleanly (idle <-> run, respecting active kick clip)
        if (attackTimer <= 0.0f && playerAnim.clips.size() > 1) {
            const auto& clips = archetypes[spec.archetypeIdx].clips;
            int desiredClip = moving ? clips.run : clips.idle;
            if (playerAnim.activeClipIndex != desiredClip) {
                playerAnim.CrossFadeToClip(desiredClip, 0.14f, true);
            }
        }
        playerAnim.playbackSpeed = (attackTimer > 0.0f) ? 1.45f : (moving ? 1.25f : 1.0f);
        playerAnim.Update(dt, cpuMode);

        // Spawn queued staggered reinforcements smoothly
        if (!spawnQueue.empty()) {
            float minDelay = 9999.0f;
            for (const auto& p : spawnQueue) minDelay = std::min(minDelay, p.delay);
            // If player cleared active group early, bring next wave squad forward smoothly
            const int earlyAlive = CountAliveEnemies();
            float speedUp = (earlyAlive == 0 && minDelay > 0.9f) ? (minDelay - 0.9f) : 0.0f;
            for (auto& p : spawnQueue) p.delay -= (dt + speedUp);

            bool spawnedAny = false;
            for (auto it = spawnQueue.begin(); it != spawnQueue.end();) {
                if (it->delay <= 0.0f) {
                    SmashEnemyUnit u = CreateEnemyUnit(it->kind, it->slotIdx, it->totalSlots, wave, it->tint, cpuMode);
                    SpawnShockwave(Vec3(u.pos.x, 0.06f, u.pos.z), 2.4f, 9.0f, ColorRGB(1.0f, 0.25f, 0.70f));
                    enemies.push_back(std::move(u));
                    spawnedAny = true;
                    it = spawnQueue.erase(it);
                } else {
                    ++it;
                }
            }
            if (spawnedAny) PlaySFX(SmashSFX::BOSS_SPAWN);
        }

        // Update Enemies AI, Attacks & Check Wave Clear
        int aliveCount = UpdateEnemies(dt, cpuMode);
        UpdateDrops(dt);
        UpdateShockwaves(dt);

        if (aliveCount == 0 && spawnQueue.empty()) {
            if (waveNextDelay <= 0.0f) {
                waveNextDelay = 2.6f;
                score += wave * 250;
                SaveHighScore();
                SetBanner("WAVE " + std::to_string(wave) + " CLEARED! +BONUS", 2.6f);
            } else {
                waveNextDelay -= dt;
                if (waveNextDelay <= 0.0f) {
                    const int clearedWave = wave;
                    wave++;
                    int healAmt = (clearedWave % 5 == 0) ? 3 : 1;
                    hp = std::min(maxHp, hp + healAmt);
                    SpawnWave(wave, cpuMode);
                }
            }
        }

        // Update 3rd-person orbit spring-arm camera + micro screen-shake
        camera.SetTarget(playerPos + Vec3(0, giantTimer > 0.0f ? 1.45f : 1.15f, 0));
        float floorUnderCam = ComputeSupportGroundY(camera.GetPosition().x, camera.GetPosition().z, camera.GetPosition().y + 1.0f, 0.35f) + 0.35f;
        camera.minFloorY = std::max(0.35f, floorUnderCam);
        camera.Update(dt, camIn);
        UpdateEngineCamera(dt);
    }

    void RenderSceneGPU(SurrealHybridRenderer& renderer, int winW, int winH, bool shadowsEnabled,
                        uint32_t* hudFB, bool refreshHud, float currentFps)
    {
        if (renderer.backend != RenderBackend::GPU_OPENGL || !renderer.isGLInitialized) return;
        const auto& curSky = skyProfiles[skyProfileIdx % 4];
        const Mat4 vpMat = engine.GetProjectionMatrix() * engine.GetViewMatrix();

        // 1. Directional CSM Shadow Pass
        if (shadowsEnabled && renderer.shaderQuality >= 1) {
            Vec3 camFwd = (engine.cameraTarget - engine.cameraPos).Normalized();
            renderer.BeginShadowPass(engine.world.sunLight.direction, playerPos, camFwd);
            const int numCascades = (renderer.shaderQuality >= 2) ? SurrealHybridRenderer::NUM_SHADOW_CASCADES : 2;
            for (int cascade = 0; cascade < numCascades; ++cascade) {
                if (cascade > 0 && !renderer.BeginShadowCascade(cascade)) continue;
                RenderStaticWorldShadowPass(renderer);
                RenderFighterShadow(renderer, playerAnim, GetPlayerWorldMatrix());
                for (const auto& e : enemies) {
                    if (e.alive) RenderFighterShadow(renderer, e.anim, GetEnemyWorldMatrix(e));
                }
            }
            renderer.EndShadowPass();
        } else {
            renderer.shadowMapReady = false;
        }

        renderer.BeginFrame(curSky.horizonColor);

        // Dynamic point lights: arena ring beacons + player fury aura + boss glow
        std::vector<PointLight> lights;
        lights.push_back({ Vec3(playerPos.x, playerPos.y + 2.2f, playerPos.z),
                           (furyTimer > 0.0f) ? ColorRGB(3.6f, 2.5f, 0.6f) : ColorRGB(1.2f, 2.2f, 3.0f),
                           (furyTimer > 0.0f) ? 1.4f : 0.9f, 14.0f });
        lights.push_back({ Vec3(-16.0f, 5.0f, -16.0f), ColorRGB(0.6f, 2.8f, 3.4f), 1.0f, 28.0f });
        lights.push_back({ Vec3( 16.0f, 5.0f,  16.0f), ColorRGB(3.4f, 0.8f, 2.4f), 1.0f, 28.0f });
        for (const auto& e : enemies) {
            if (e.alive && e.isBoss) {
                lights.push_back({ Vec3(e.pos.x, e.pos.y + 2.8f, e.pos.z), ColorRGB(3.8f, 0.9f, 0.5f), 1.5f, 18.0f });
                break;
            }
        }
        renderer.SetGlobalUniforms(
            engine.world.sunLight.direction,
            engine.world.sunLight.color * engine.world.sunLight.intensity,
            engine.world.ambientLight,
            engine.cameraPos,
            curSky.horizonColor,
            elapsedTime,
            lights
        );

        engine.renderedMeshlets = 0;
        engine.culledMeshlets = 0;

        // 2. Render Static Arena World
        RenderStaticWorldGPU(renderer, vpMat);

        // 3. Render Player 3D Cat Fighter
        RenderFighterGPU(renderer, playerAnim, playerCpuMesh, GetPlayerWorldMatrix(), vpMat, GetPlayerMaterial());

        // 4. Render Enemies & Boss
        for (const auto& e : enemies) {
            if (!e.alive) continue;
            PBRMaterial em;
            em.albedo = (e.hitFlashTimer > 0.0f) ? ColorRGB(2.0f, 0.25f, 0.25f) : e.tint;
            em.emissive = (e.windupTimer > 0.0f) ? ColorRGB(0.95f, 0.2f, 0.15f)
                        : (e.isBoss ? ColorRGB(0.45f, 0.08f, 0.05f) : ColorRGB(0, 0, 0));
            em.roughness = 0.40f;
            RenderFighterGPU(renderer, e.anim, e.cpuMesh, GetEnemyWorldMatrix(e), vpMat, em);
        }

        // 5. Render Power-Up Orbs
        for (const auto& d : drops) {
            if (!d.active || !dropOrbMesh) continue;
            float bob = 0.55f + 0.18f * sinf(elapsedTime * 4.5f + d.age);
            Mat4 dm = Mat4::Translation(Vec3(d.pos.x, d.pos.y + bob, d.pos.z)) *
                      Mat4::FromQuat(Quat::FromAxisAngle(Vec3(0, 1, 0), elapsedTime * 2.5f));
            PBRMaterial dmat;
            dmat.albedo = d.color;
            dmat.emissive = d.color * 1.4f;
            dmat.metallic = 0.2f;
            dmat.roughness = 0.15f;
            renderer.RenderMesh(dropOrbMesh, dm, vpMat, dmat, false);
        }

        // 6. Render Sky Dome
        renderer.RenderSky(vpMat.Inversed(), curSky.zenithColor, curSky.horizonColor, curSky.groundColor,
                           curSky.sunDir, curSky.sunColor * curSky.sunIntensity, curSky.isNight);

        // 7. Render Transparent Ground Shadows + Arena Ring + Expanding Slam Shockwaves
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        if (shadowDiscMesh) {
            PBRMaterial smat;
            smat.albedo = ColorRGB(0.01f, 0.01f, 0.02f);
            smat.roughness = 1.0f;
            float pScale = std::max(0.45f, (giantTimer > 0.0f ? 1.45f : 1.0f) / (1.0f + std::max(0.0f, playerPos.y) * 0.45f));
            Mat4 ps = Mat4::Translation(Vec3(playerPos.x, 0.015f, playerPos.z)) * Mat4::Scaling(Vec3(pScale, 1.0f, pScale));
            renderer.RenderMesh(shadowDiscMesh, ps, vpMat, smat, false);

            for (const auto& e : enemies) {
                if (!e.alive) continue;
                float es = e.isBoss ? 3.2f : 1.0f;
                Mat4 em = Mat4::Translation(Vec3(e.pos.x, 0.015f, e.pos.z)) * Mat4::Scaling(Vec3(es, 1.0f, es));
                renderer.RenderMesh(shadowDiscMesh, em, vpMat, smat, false);
            }
        }

        if (ringMesh) {
            // Subtle glowing boundary ring
            PBRMaterial bmat;
            bmat.albedo = ColorRGB(0.0f, 0.85f, 1.0f);
            bmat.emissive = ColorRGB(0.0f, 0.85f, 1.0f) * 0.9f;
            bmat.doubleSided = true;
            Mat4 bm = Mat4::Translation(Vec3(0, 0.04f, 0)) * Mat4::Scaling(Vec3(ARENA_RADIUS, 1.0f, ARENA_RADIUS));
            renderer.RenderMesh(ringMesh, bm, vpMat, bmat, false);

            for (const auto& sw : shockwaves) {
                if (!sw.active) continue;
                PBRMaterial fxMat;
                float fade = 1.0f - (sw.radius / std::max(0.1f, sw.maxRadius));
                fxMat.albedo = sw.color;
                fxMat.emissive = sw.color * (2.6f * std::max(0.1f, fade));
                fxMat.doubleSided = true;
                Mat4 fxM = Mat4::Translation(Vec3(sw.origin.x, sw.origin.y + 0.05f, sw.origin.z)) *
                           Mat4::Scaling(Vec3(sw.radius, 1.0f, sw.radius));
                renderer.RenderMesh(ringMesh, fxM, vpMat, fxMat, false);
            }
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        renderer.EndHDRScenePass();

        if (refreshHud && hudFB) {
            std::fill(hudFB, hudFB + static_cast<size_t>(winW) * winH, 0x00000000);
            RenderGameHUD(hudFB, winW, winH, vpMat, currentFps, renderer.GetBackendName());
        }
        renderer.BlitHUDOverlay(hudFB, winW, winH, refreshHud);
        renderer.Present();
    }

    void RenderSceneCPU(float dt, uint32_t* fb, int winW, int winH, float currentFps, const std::string& backendName) {
        EnsureMeshModeConsistency(true);
        const Mat4 vpMat = engine.GetProjectionMatrix() * engine.GetViewMatrix();

        // Clear software framebuffer & depth buffer cleanly
        engine.Clear();
        for (EntityID eid : engine.world.entities) {
            if (!useSeoulArena && std::find(seoulEntities.begin(), seoulEntities.end(), eid) != seoulEntities.end()) {
                continue;
            }
            auto itMr = engine.world.meshRenderers.find(eid);
            auto itTr = engine.world.transforms.find(eid);
            if (itMr != engine.world.meshRenderers.end() && itTr != engine.world.transforms.end() && itMr->second.mesh) {
                engine.DrawCustomMesh(itMr->second.mesh, itTr->second.GetWorldMatrix(), itMr->second.material);
            }
        }

        // Player
        std::shared_ptr<SurrealMesh> pMesh = playerCpuMesh ? playerCpuMesh : playerAnim.activeDeformedMesh;
        if (pMesh) engine.DrawCustomMesh(pMesh, GetPlayerWorldMatrix(), GetPlayerMaterial());

        // Enemies
        for (const auto& e : enemies) {
            if (!e.alive) continue;
            std::shared_ptr<SurrealMesh> emesh = e.cpuMesh ? e.cpuMesh : e.anim.activeDeformedMesh;
            if (!emesh) continue;
            PBRMaterial em;
            em.albedo = (e.hitFlashTimer > 0.0f) ? ColorRGB(2.0f, 0.25f, 0.25f) : e.tint;
            em.roughness = 0.45f;
            engine.DrawCustomMesh(emesh, GetEnemyWorldMatrix(e), em);
        }

        // Drops & Shockwave Rings
        for (const auto& d : drops) {
            if (!d.active || !dropOrbMesh) continue;
            float bob = 0.55f + 0.18f * sinf(elapsedTime * 4.5f + d.age);
            Mat4 dm = Mat4::Translation(Vec3(d.pos.x, d.pos.y + bob, d.pos.z));
            PBRMaterial dmat;
            dmat.albedo = d.color;
            dmat.emissive = d.color * 1.1f;
            engine.DrawCustomMesh(dropOrbMesh, dm, dmat);
        }
        if (ringMesh) {
            for (const auto& sw : shockwaves) {
                if (!sw.active) continue;
                PBRMaterial fxMat;
                fxMat.albedo = sw.color;
                fxMat.emissive = sw.color * 1.4f;
                Mat4 fxM = Mat4::Translation(Vec3(sw.origin.x, sw.origin.y + 0.05f, sw.origin.z)) *
                           Mat4::Scaling(Vec3(sw.radius, 1.0f, sw.radius));
                engine.DrawCustomMesh(ringMesh, fxM, fxMat);
            }
        }

        (void)dt;
        RenderGameHUD(fb, winW, winH, vpMat, currentFps, backendName);
    }

    void RenderGameHUD(uint32_t* fb, int w, int h, const Mat4& vpMat, float fps, const std::string& backendName) const {
        if (!fb) return;
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        char buf[160];

        // 1. Top-Left Fighter Status Panel
        SurrealHUD::DrawPanel(fb, w, h, 14, 14, 340, 148, 0x070C18, 0.88f);
        SurrealHUD::DrawString(fb, w, h, 24, 24, "SMASH CATS 3D // SURREAL EDITION", 0xFF00FFCC, 1);

        std::snprintf(buf, sizeof(buf), "CAT: %-13s  [1/2/3]", spec.name);
        SurrealHUD::DrawString(fb, w, h, 24, 42, buf, 0xFFFFCC00, 1);

        std::snprintf(buf, sizeof(buf), "LIVES: %d    HP: %d/%d", lives, hp, maxHp);
        SurrealHUD::DrawString(fb, w, h, 24, 58, buf, lives <= 2 ? 0xFFFF4466 : 0xFFFF55EE, 1);

        uint32_t hpCol = (hp >= 6) ? 0xFF00FF88 : (hp >= 3 ? 0xFFFFAA00 : 0xFFFF3344);
        SurrealHUD::DrawProgressBar(fb, w, h, 24, 72, 316, 12, static_cast<float>(hp) / maxHp, hpCol, 0xFF221422);

        std::snprintf(buf, sizeof(buf), "FUR: %-10s [C]   SLAM: %s",
                      kFurPresetNames[furPresetIdx],
                      (slamCooldown <= 0.01f) ? "READY" : "COOL");
        SurrealHUD::DrawString(fb, w, h, 24, 94, buf, (slamCooldown <= 0.01f) ? 0xFF00FFCC : 0xFFFF6666, 1);

        if (furyTimer > 0.0f || giantTimer > 0.0f) {
            std::snprintf(buf, sizeof(buf), "BUFF:%s%.1fs %s%.1fs",
                          furyTimer > 0.0f ? " FURY " : "", furyTimer > 0.0f ? furyTimer : 0.0f,
                          giantTimer > 0.0f ? " GIANT " : "", giantTimer > 0.0f ? giantTimer : 0.0f);
            SurrealHUD::DrawString(fb, w, h, 24, 112, buf, 0xFFFFEE33, 1);
        } else {
            std::snprintf(buf, sizeof(buf), "ARENA: %s [V]", useSeoulArena ? "SEOUL PBR" : "CYBER ARENA");
            SurrealHUD::DrawString(fb, w, h, 24, 112, buf, 0xFF88CCFF, 1);
        }

        std::snprintf(buf, sizeof(buf), "SFX: %s [M]  |  %s [F9]",
                      audioMuted ? "OFF" : "ON", backendName.c_str());
        SurrealHUD::DrawString(fb, w, h, 24, 132, buf, 0xFF99BBDD, 1);

        // 2. Top-Right Score / Wave / FPS Card
        int rx = std::max(365, w - 285);
        SurrealHUD::DrawPanel(fb, w, h, rx, 14, w - rx - 14, 108, 0x070C18, 0.88f);
        if (!spawnQueue.empty()) {
            std::snprintf(buf, sizeof(buf), "WAVE: %d   FOES: %d(+%zu)", wave, CountAliveEnemies(), spawnQueue.size());
        } else {
            std::snprintf(buf, sizeof(buf), "WAVE: %d   FOES: %d", wave, CountAliveEnemies());
        }
        SurrealHUD::DrawString(fb, w, h, rx + 12, 24, buf, 0xFF00FFCC, 1);
        std::snprintf(buf, sizeof(buf), "SCORE: %d", score);
        SurrealHUD::DrawString(fb, w, h, rx + 12, 42, buf, 0xFFFFFFFF, 1);
        std::snprintf(buf, sizeof(buf), "BEST:  %d   KILLS: %d", std::max(score, highScore), kills);
        SurrealHUD::DrawString(fb, w, h, rx + 12, 58, buf, 0xFFFFD700, 1);
        if (comboCount > 1) {
            std::snprintf(buf, sizeof(buf), "COMBO x%d!", std::min(8, comboCount));
            SurrealHUD::DrawString(fb, w, h, rx + 12, 76, buf, 0xFFFF44FF, 1);
        } else {
            std::snprintf(buf, sizeof(buf), "FPS: %.1f", fps);
            SurrealHUD::DrawString(fb, w, h, rx + 12, 76, buf, fps >= 50.0f ? 0xFF00FF88 : 0xFFFFCC00, 1);
        }
        std::snprintf(buf, sizeof(buf), "SKY: %s [N]", skyProfiles[skyProfileIdx].name);
        SurrealHUD::DrawString(fb, w, h, rx + 12, 94, buf, 0xFF88AABB, 1);

        // 3. Boss HP Bar & Wave/Action Banner (Top Center)
        for (const auto& e : enemies) {
            if (e.alive && e.isBoss && w > 640) {
                int bw = 360, bx = (w - bw) / 2;
                SurrealHUD::DrawPanel(fb, w, h, bx, 14, bw, 46, 0x1A0610, 0.92f);
                SurrealHUD::DrawString(fb, w, h, bx + 95, 22, "BOSS: CHAOS DRAGON", 0xFFFF4466, 1);
                SurrealHUD::DrawProgressBar(fb, w, h, bx + 16, 38, bw - 32, 12,
                                            static_cast<float>(e.hp) / std::max(1, e.maxHp), 0xFFFF2244, 0xFF331118);
                break;
            }
        }
        if (statusBannerTimer > 0.0f && !statusBanner.empty()) {
            int sw = std::min(w - 40, static_cast<int>(statusBanner.size()) * 8 + 28);
            int sx = (w - sw) / 2;
            int sy = 70;
            SurrealHUD::DrawPanel(fb, w, h, sx, sy, sw, 26, 0x091526, 0.90f);
            SurrealHUD::DrawString(fb, w, h, sx + 14, sy + 9, statusBanner, 0xFF00FFCC, 1);
        }

        // 4. Floating Projected 3D Health Bars above Enemies
        for (const auto& e : enemies) {
            if (!e.alive) continue;
            float topOff = e.isBoss ? 3.55f : 1.75f;
            int sx = 0, sy = 0;
            if (SurrealHUD::WorldToScreen(e.pos + Vec3(0, topOff, 0), vpMat, w, h, sx, sy)) {
                int bw = e.isBoss ? 96 : 56;
                int bx = std::clamp(sx - bw / 2, 4, w - bw - 4);
                int by = std::clamp(sy - 12, 4, h - 18);
                SurrealHUD::DrawProgressBar(fb, w, h, bx, by, bw, 6,
                                            static_cast<float>(e.hp) / std::max(1, e.maxHp),
                                            e.hitFlashTimer > 0.0f ? 0xFFFFFFFF : (e.isBoss ? 0xFFFF2255 : 0xFFFF6622),
                                            0xFF1A0A0A);
            }
        }

        // 5. Controls Footer
        if (h > 300) {
            SurrealHUD::DrawPanel(fb, w, h, 14, h - 34, w - 28, 22, 0x050912, 0.80f);
            SurrealHUD::DrawString(fb, w, h, 24, h - 27,
                                   "[WASD] Move | [SPACE x1/2/3] Jump/Flip/Slam | [LMB/J] Combo | [F/RMB] Heavy | [RMB Drag/Q/E] Cam | [1-3] Cat | [V] Arena | [P] Pause",
                                   0xFFAACCDD, 1);
        }

        // 6. Pause / Game Over Center Overlay
        if (isPaused || isGameOver) {
            int cw = 420, ch = 150;
            int cx = (w - cw) / 2, cy = (h - ch) / 2;
            SurrealHUD::DrawPanel(fb, w, h, cx, cy, cw, ch, isGameOver ? 0x1E050A : 0x081224, 0.95f);
            SurrealHUD::DrawString(fb, w, h, cx + 122, cy + 28,
                                   isGameOver ? "GAME OVER - DEFEATED!" : "GAME PAUSED [P]",
                                   isGameOver ? 0xFFFF3355 : 0xFF00FFCC, 1);
            std::snprintf(buf, sizeof(buf), "FINAL SCORE: %d   |   BEST: %d", score, std::max(score, highScore));
            SurrealHUD::DrawString(fb, w, h, cx + 70, cy + 62, buf, 0xFFFFD700, 1);
            SurrealHUD::DrawString(fb, w, h, cx + 78, cy + 102,
                                   isGameOver ? "Press [ENTER] or [R] to Respawn!" : "Press [P] to Resume | [R] Restart",
                                   0xFFFFFFFF, 1);
        }
    }

    int CountAliveEnemies() const {
        int c = 0;
        for (const auto& e : enemies) if (e.alive) c++;
        return c;
    }

private:
    SmashEnemyUnit CreateEnemyUnit(SmashEnemyKind kind, int idx, int total, int waveNum, const ColorRGB& tint, bool cpuMode) const {
        const auto& es = kEnemySpecs[static_cast<int>(kind)];
        const auto& arch = archetypes[static_cast<int>(kind)];

        SmashEnemyUnit u;
        u.alive = true;
        u.kind = kind;
        u.baseScale = arch.normScale;
        u.yawOffset = arch.yawOffset;
        u.radius = es.radius;
        u.maxHp = es.baseHp + (waveNum - 1) * (es.isBoss ? 12 : 2);
        u.hp = u.maxHp;
        u.isBoss = es.isBoss;
        u.tint = tint;
        u.clips = arch.clips;
        u.roamPhase = static_cast<float>(idx) * 1.37f;

        float angle = (total > 0) ? (static_cast<float>(idx) * TWO_PI / static_cast<float>(total)) : 0.0f;
        float dist = es.isBoss ? 16.5f : (11.5f + (idx % 3) * 2.8f);
        u.pos = Vec3(cosf(angle) * dist, 0.0f, sinf(angle) * dist);
        u.yaw = atan2f(playerPos.x - u.pos.x, playerPos.z - u.pos.z);

        arch.Instantiate(u.anim, u.cpuMesh, cpuMode);
        u.anim.PlayClip(arch.clips.idle, true);
        u.anim.currentTime = fmodf(static_cast<float>(idx) * 0.43f, 2.0f);
        u.anim.Update(0.0f, cpuMode);
        return u;
    }

    PBRMaterial GetPlayerMaterial() const {
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        PBRMaterial pm;
        if (hitFlashTimer > 0.0f) {
            pm.albedo = ColorRGB(2.0f, 0.25f, 0.25f);
            pm.emissive = ColorRGB(0.8f, 0.1f, 0.1f);
        } else if (invulnTimer > 0.0f && fmodf(elapsedTime * 14.0f, 2.0f) < 1.0f) {
            pm.albedo = spec.baseTint * kFurPresets[furPresetIdx] * 1.45f;
            pm.emissive = ColorRGB(0.25f, 0.8f, 0.9f);
        } else {
            pm.albedo = spec.baseTint * kFurPresets[furPresetIdx];
            if (furyTimer > 0.0f) pm.emissive = ColorRGB(0.55f, 0.35f, 0.05f);
        }
        pm.metallic = 0.08f;
        pm.roughness = 0.40f;
        return pm;
    }

    Mat4 GetPlayerWorldMatrix() const {
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        const auto& arch = archetypes[spec.archetypeIdx];
        float sizeMult = (giantTimer > 0.0f) ? 1.45f : 1.0f;
        float s = arch.normScale * sizeMult;

        float attackPitch = 0.0f;
        float attackRoll = 0.0f;
        if (attackTimer > 0.0f) {
            float t = sinf((attackTimer / (attackHeavy ? 0.36f : 0.24f)) * M_PI);
            attackPitch = attackHeavy ? (t * 0.28f) : (t * 0.18f);
            attackRoll  = attackHeavy ? (t * 0.45f) : (t * ((attackComboAlt == 0) ? -0.22f : 0.22f));
        }

        Quat qYaw = Quat::FromAxisAngle(Vec3(0, 1, 0), playerYaw + arch.yawOffset);
        Quat qFlip = (jumpStage == 2)
            ? Quat::FromAxisAngle(Vec3(1, 0, 0), flipAngleDeg * (M_PI / 180.0f))
            : Quat::FromAxisAngle(Vec3(1, 0, 0), attackPitch) * Quat::FromAxisAngle(Vec3(0, 0, 1), attackRoll);

        return Mat4::Translation(playerPos) * Mat4::FromQuat(qYaw * qFlip) * Mat4::Scaling(Vec3(s, s, s));
    }

    Mat4 GetEnemyWorldMatrix(const SmashEnemyUnit& e) const {
        float atkLunge = (e.windupTimer > 0.0f) ? (-0.20f) : 0.0f;
        Quat q = Quat::FromAxisAngle(Vec3(0, 1, 0), e.yaw + e.yawOffset) * Quat::FromAxisAngle(Vec3(1, 0, 0), atkLunge);
        return Mat4::Translation(e.pos) * Mat4::FromQuat(q) * Mat4::Scaling(Vec3(e.baseScale, e.baseScale, e.baseScale));
    }

    void ResolveObstacleHorizontal(const Vec3& oldPos, Vec3& nextPos, float radius, float height) const {
        for (const auto& obs : obstacles) {
            if (oldPos.y < obs.topY - 0.12f && (oldPos.y + height) > obs.aabb.min.y + 0.05f) {
                if (nextPos.x >= obs.aabb.min.x - radius && nextPos.x <= obs.aabb.max.x + radius &&
                    oldPos.z  >= obs.aabb.min.z - radius && oldPos.z  <= obs.aabb.max.z + radius) {
                    nextPos.x = oldPos.x;
                }
                if (oldPos.x  >= obs.aabb.min.x - radius && oldPos.x  <= obs.aabb.max.x + radius &&
                    nextPos.z >= obs.aabb.min.z - radius && nextPos.z <= obs.aabb.max.z + radius) {
                    nextPos.z = oldPos.z;
                }
            }
        }
    }

    float ComputeSupportGroundY(float x, float z, float curY, float footR) const {
        float groundY = 0.0f;
        for (const auto& obs : obstacles) {
            if (x >= obs.aabb.min.x - footR && x <= obs.aabb.max.x + footR &&
                z >= obs.aabb.min.z - footR && z <= obs.aabb.max.z + footR) {
                if (curY >= obs.topY - 0.40f && obs.topY > groundY) {
                    groundY = obs.topY;
                }
            }
        }
        return groundY;
    }

    static void ClampInsideArena(Vec3& p, float maxR) {
        float r2 = p.x * p.x + p.z * p.z;
        if (r2 > maxR * maxR) {
            float inv = maxR / sqrtf(r2);
            p.x *= inv;
            p.z *= inv;
        }
    }

    void UpdateEngineCamera(float dt) {
        (void)dt;
        Vec3 shakeOff(0, 0, 0);
        if (screenShake > 0.001f) {
            shakeOff = Vec3(sinf(elapsedTime * 57.0f) * screenShake * 0.18f,
                            cosf(elapsedTime * 71.0f) * screenShake * 0.14f, 0.0f);
        }
        engine.cameraPos = camera.GetPosition() + shakeOff;
        engine.cameraTarget = camera.GetTarget() + shakeOff * 0.5f;
        engine.cameraUp = camera.GetUp();
    }

    void OnGroundSlamImpact(float groundY) {
        const auto& spec = kSmashClasses[static_cast<int>(playerClass)];
        float rad = spec.slamRadius * (giantTimer > 0.0f ? 1.3f : 1.0f);
        int dmg = spec.heavyDamage + (furyTimer > 0.0f ? 3 : 1);
        slamCooldown = 4.5f;
        screenShake = 0.85f;
        SpawnShockwave(Vec3(playerPos.x, groundY + 0.05f, playerPos.z), rad, 16.0f, ColorRGB(0.0f, 1.0f, 0.90f));
        SpawnShockwave(Vec3(playerPos.x, groundY + 0.08f, playerPos.z), rad * 0.72f, 12.0f, ColorRGB(1.0f, 0.35f, 0.90f));
        PlaySFX(SmashSFX::GROUND_SLAM);
        ApplyRadialAttack(playerPos, rad, dmg, 12.5f);
    }

    void ApplyDamageToEnemy(SmashEnemyUnit& e, int dmg, const Vec3& pushDir, float knockback) {
        if (!e.alive) return;
        e.hp -= dmg;
        e.hitFlashTimer = 0.16f;
        const bool armored = (e.isBoss || e.kind == SmashEnemyKind::CYBER_BEAST);
        const float actualKnock = armored ? (knockback * 0.22f) : knockback;
        e.vel = e.vel + pushDir * actualKnock;
        if (!armored && knockback >= 5.0f && e.pos.y <= 0.1f) {
            e.vel.y = std::max(e.vel.y, 2.4f);
        }
        // Only non-boss units lose windup poise when struck by Heavy attacks
        if (!e.isBoss && e.windupTimer > 0.0f && (!armored || knockback >= 5.0f)) {
            e.windupTimer = 0.0f;
        }

        comboCount++;
        comboTimer = 2.4f;
        score += dmg * 15 * std::min(8, comboCount);
        PlaySFX(SmashSFX::HIT_IMPACT);

        if (e.hp <= 0) {
            e.alive = false;
            kills++;
            score += kEnemySpecs[static_cast<int>(e.kind)].scoreValue * std::min(8, std::max(1, comboCount));
            SaveHighScore();
            MaybeSpawnDrop(e.pos, e.isBoss);
            SpawnShockwave(e.pos, e.isBoss ? 5.5f : 2.2f, 10.0f, ColorRGB(1.0f, 0.75f, 0.25f));
        }
    }

    int ApplyMeleeConeAttack(const Vec3& origin, const Vec3& fwd, float range, float minDot, int dmg, float knock) {
        int hitCount = 0;
        for (auto& e : enemies) {
            if (!e.alive) continue;
            Vec3 toE(e.pos.x - origin.x, 0.0f, e.pos.z - origin.z);
            float dist = toE.Length();
            if (dist > range + e.radius) continue;
            Vec3 dir = (dist > 0.01f) ? (toE * (1.0f / dist)) : fwd;
            if (dist < 1.3f || dir.Dot(fwd) >= minDot) {
                ApplyDamageToEnemy(e, dmg, dir, knock);
                hitCount++;
            }
        }
        return hitCount;
    }

    int ApplyRadialAttack(const Vec3& origin, float range, int dmg, float knock) {
        int hitCount = 0;
        for (auto& e : enemies) {
            if (!e.alive) continue;
            Vec3 toE(e.pos.x - origin.x, 0.0f, e.pos.z - origin.z);
            float dist = toE.Length();
            if (dist <= range + e.radius) {
                Vec3 dir = (dist > 0.01f) ? (toE * (1.0f / dist)) : Vec3(0, 0, 1);
                ApplyDamageToEnemy(e, dmg, dir, knock);
                hitCount++;
            }
        }
        return hitCount;
    }

    void MaybeSpawnDrop(const Vec3& where, bool guaranteed) {
        if (!guaranteed && (rand() % 100) >= 22) return;
        int roll = rand() % 100;
        SmashDropKind dk = SmashDropKind::CATNIP_HEAL;
        ColorRGB col(0.2f, 1.0f, 0.45f);
        if (guaranteed && roll < 30) {
            dk = SmashDropKind::EXTRA_LIFE; col = ColorRGB(1.0f, 0.25f, 0.95f);
        } else if (roll < 50) {
            dk = SmashDropKind::CATNIP_HEAL; col = ColorRGB(0.2f, 1.0f, 0.45f);
        } else if (roll < 80) {
            dk = SmashDropKind::FURY_ORB; col = ColorRGB(1.0f, 0.85f, 0.15f);
        } else {
            dk = SmashDropKind::GIANT_ORB; col = ColorRGB(0.1f, 0.95f, 1.0f);
        }
        drops.push_back({ true, dk, Vec3(where.x, 0.0f, where.z), 0.0f, col });
    }

    void SpawnShockwave(const Vec3& origin, float maxR, float speed, const ColorRGB& col) {
        shockwaves.push_back({ true, origin, 0.35f, maxR, speed, col });
    }

    int UpdateEnemies(float dt, bool cpuMode) {
        int aliveCount = 0;
        for (auto& e : enemies) {
            if (!e.alive) continue;
            aliveCount++;

            if (e.hitFlashTimer > 0.0f) e.hitFlashTimer = std::max(0.0f, e.hitFlashTimer - dt);
            if (e.attackCooldown > 0.0f) e.attackCooldown = std::max(0.0f, e.attackCooldown - dt);
            if (e.bossStompCooldown > 0.0f) e.bossStompCooldown = std::max(0.0f, e.bossStompCooldown - dt);

            const auto& es = kEnemySpecs[static_cast<int>(e.kind)];
            Vec3 toPlayer(playerPos.x - e.pos.x, 0.0f, playerPos.z - e.pos.z);
            float dist = toPlayer.Length();
            Vec3 dir = (dist > 0.001f) ? (toPlayer * (1.0f / dist)) : Vec3(0, 0, 1);

            bool isChasing = false;
            if (e.windupTimer > 0.0f) {
                e.windupTimer -= dt;
                if (e.windupTimer <= 0.0f) {
                    // Strike!
                    int hitDmg = es.damage;
                    if (!e.isBoss && wave >= 6 && e.kind == SmashEnemyKind::CYBER_BEAST) hitDmg += 1;
                    if (!e.isBoss && wave >= 4 && e.kind == SmashEnemyKind::CYBER_PROWLER) hitDmg += 1;
                    if (dist <= es.attackRange + 0.75f) {
                        HurtPlayer(hitDmg, dir);
                    }
                    e.vel = e.vel + dir * 5.2f;
                    e.attackCooldown = e.isBoss ? 1.35f : 0.92f;
                }
            } else if (dist > es.attackRange) {
                isChasing = true;
                Vec3 steer = dir;
                // Gentle flank separation so enemies don't stack identically on 1 coordinate
                steer.x += cosf(elapsedTime * 1.6f + e.roamPhase) * 0.28f;
                steer.z += sinf(elapsedTime * 1.6f + e.roamPhase) * 0.28f;
                steer = steer.Normalized();
                e.pos.x += steer.x * es.speed * dt;
                e.pos.z += steer.z * es.speed * dt;
                e.yaw = atan2f(dir.x, dir.z);
            } else if (e.attackCooldown <= 0.0f) {
                e.yaw = atan2f(dir.x, dir.z);
                e.windupTimer = e.isBoss ? 0.42f : (e.kind == SmashEnemyKind::CYBER_BEAST ? 0.36f : 0.24f);
                if (e.anim.clips.size() > 1) e.anim.CrossFadeToClip(e.clips.atk1, 0.10f, false);
            }

            // Boss periodic Fire Shockwave Nova! (jump over it with SPACE!)
            if (e.isBoss && e.bossStompCooldown <= 0.0f && dist < 9.5f) {
                e.bossStompCooldown = 4.5f;
                SpawnShockwave(Vec3(e.pos.x, 0.06f, e.pos.z), 8.0f, 12.0f, ColorRGB(1.0f, 0.25f, 0.10f));
                if (dist < 6.8f && playerPos.y < 1.35f) {
                    HurtPlayer(3, dir);
                }
            }

            // Knockback & vertical gravity
            e.pos = e.pos + e.vel * dt;
            e.vel.x *= std::max(0.0f, 1.0f - dt * 6.0f);
            e.vel.z *= std::max(0.0f, 1.0f - dt * 6.0f);
            if (e.pos.y > 0.0f || e.vel.y > 0.0f) {
                e.vel.y += GRAVITY * dt;
                if (e.pos.y <= 0.0f) { e.pos.y = 0.0f; e.vel.y = 0.0f; }
            }
            ClampInsideArena(e.pos, ARENA_RADIUS - e.radius);

            if (e.windupTimer <= 0.0f && e.anim.clips.size() > 1) {
                int wantClip = isChasing ? e.clips.run : e.clips.idle;
                if (e.anim.activeClipIndex != wantClip) {
                    e.anim.CrossFadeToClip(wantClip, 0.16f, true);
                }
            }
            e.anim.Update(dt, cpuMode);
        }
        return aliveCount;
    }

public:
    void HurtPlayer(int dmg, const Vec3& pushDir) {
        if (invulnTimer > 0.0f || isGameOver) return;
        hp -= dmg;
        hitFlashTimer = 0.20f;
        invulnTimer = 0.28f;
        screenShake = 0.45f;
        playerPos.x += pushDir.x * 0.55f;
        playerPos.z += pushDir.z * 0.55f;
        ClampInsideArena(playerPos, ARENA_RADIUS - 0.6f);
        PlaySFX(SmashSFX::PLAYER_HURT);

        if (hp <= 0) {
            lives--;
            if (lives <= 0) {
                hp = 0;
                lives = 0;
                isGameOver = true;
                SaveHighScore();
                PlaySFX(SmashSFX::GAME_OVER);
            } else {
                hp = maxHp;
                invulnTimer = 1.4f;
                SpawnShockwave(playerPos, 4.5f, 14.0f, ColorRGB(1.0f, 0.2f, 0.9f));
                SetBanner("RESPAWN! " + std::to_string(lives) + " LIVES LEFT", 2.4f);
            }
        }
    }

private:
    void UpdateDrops(float dt) {
        for (auto& d : drops) {
            if (!d.active) continue;
            d.age += dt;
            if (d.age > 24.0f) { d.active = false; continue; }
            float dx = playerPos.x - d.pos.x;
            float dz = playerPos.z - d.pos.z;
            if (dx * dx + dz * dz < 1.9f * 1.9f && std::abs(playerPos.y - d.pos.y) < 1.8f) {
                d.active = false;
                PlaySFX(SmashSFX::PICKUP_ORB);
                switch (d.kind) {
                    case SmashDropKind::CATNIP_HEAL:
                        hp = std::min(maxHp, hp + 2);
                        SetBanner("+2 HP CATNIP!", 1.8f);
                        break;
                    case SmashDropKind::FURY_ORB:
                        furyTimer = 10.0f;
                        SetBanner("FURY BOOST (2x DAMAGE)!", 2.2f);
                        break;
                    case SmashDropKind::GIANT_ORB:
                        giantTimer = 12.0f;
                        SetBanner("GIANT CAT POWERUP!", 2.2f);
                        break;
                    case SmashDropKind::EXTRA_LIFE:
                        lives = std::min(5, lives + 1);
                        SetBanner("+1 EXTRA LIFE!", 2.4f);
                        break;
                }
            }
        }
        drops.erase(std::remove_if(drops.begin(), drops.end(), [](const SmashDropOrb& o) { return !o.active; }), drops.end());
    }

    void UpdateShockwaves(float dt) {
        for (auto& s : shockwaves) {
            if (!s.active) continue;
            s.radius += s.speed * dt;
            if (s.radius >= s.maxRadius) s.active = false;
        }
        shockwaves.erase(std::remove_if(shockwaves.begin(), shockwaves.end(), [](const SmashShockwaveFX& s) { return !s.active; }), shockwaves.end());
    }

    void RenderStaticWorldShadowPass(SurrealHybridRenderer& renderer) const {
        for (EntityID eid : arenaPropEntities) {
            auto itMr = engine.world.meshRenderers.find(eid);
            auto itTr = engine.world.transforms.find(eid);
            if (itMr != engine.world.meshRenderers.end() && itTr != engine.world.transforms.end() && itMr->second.mesh) {
                renderer.RenderMeshShadow(itMr->second.mesh, itTr->second.GetWorldMatrix(), false, false);
            }
        }
    }

    static void RenderFighterShadow(SurrealHybridRenderer& renderer, const SkeletalMeshInstance& anim, const Mat4& worldMat) {
        if (anim.HasSkinData()) {
            renderer.RenderSkinnedMeshShadow(anim, worldMat);
        } else if (anim.activeDeformedMesh) {
            renderer.RenderMeshShadow(anim.activeDeformedMesh, worldMat, false, false);
        }
    }

    void RenderStaticWorldGPU(SurrealHybridRenderer& renderer, const Mat4& vpMat) {
        auto renderEnt = [&](EntityID eid) {
            auto itMr = engine.world.meshRenderers.find(eid);
            auto itTr = engine.world.transforms.find(eid);
            if (itMr == engine.world.meshRenderers.end() || itTr == engine.world.transforms.end() || !itMr->second.mesh) return;
            renderer.RenderMesh(itMr->second.mesh, itTr->second.GetWorldMatrix(), vpMat, itMr->second.material, false);
            engine.renderedMeshlets += std::max<uint32_t>(1u, static_cast<uint32_t>(itMr->second.mesh->meshlets.size()));
        };
        if (floorEntity != INVALID_ENTITY) renderEnt(floorEntity);
        for (EntityID eid : arenaPropEntities) renderEnt(eid);
        if (useSeoulArena) {
            for (EntityID eid : seoulEntities) renderEnt(eid);
        }
    }

    void RenderFighterGPU(SurrealHybridRenderer& renderer, const SkeletalMeshInstance& anim,
                          const std::shared_ptr<SurrealMesh>& fallback, const Mat4& worldMat,
                          const Mat4& vpMat, const PBRMaterial& mat)
    {
        if (anim.HasSkinData()) {
            renderer.RenderSkinnedMesh(anim, worldMat, vpMat, mat);
            if (anim.activeDeformedMesh) {
                engine.renderedMeshlets += std::max<uint32_t>(1u, static_cast<uint32_t>(anim.activeDeformedMesh->meshlets.size()));
            }
        } else if (fallback) {
            renderer.RenderMesh(fallback, worldMat, vpMat, mat, false);
        }
    }
};

} // namespace Surreal
