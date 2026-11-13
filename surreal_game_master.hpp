#pragma once
/**
 * ============================================================================
 * AION SURREAL 3D ENGINE - AUTONOMOUS AI GAME MASTER & IN-GAME CO-OP PLAYER
 *
 * Implements Aion in dual sovereign roles:
 * 1. GAME MASTER & DIRECTOR: Procedural trials, adaptive physics, weather shifts,
 *    live voice commentary via TTS, and Quantum Rewind void catch.
 * 2. AUTONOMOUS IN-GAME PLAYER (Co-Op Companion & Trial Competitor):
 *    Spawns her own 3D skeletal avatar in the world, runs and parkours alongside
 *    the human player, competes for checkpoints in trials, dances in duets,
 *    and allows instant player control swapping via [T]!
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_hud.hpp"
#include "surreal_aion_link.hpp"
#include "surreal_model_loader.hpp"
#include "surreal_hybrid_renderer.hpp"
#include "surreal_animation.hpp"
#include "surreal_ecs.hpp"
#include "surreal_editor.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <functional>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>

namespace Surreal {

enum class GMTrialType {
    NONE = 0,
    PARKOUR_SPRINT,     // Checkpoint time trial across boulevard & ramps
    QUANTUM_CORE_HUNT,  // Scavenger hunt for floating energy cores
    LUNAR_FLIGHT,       // Low-gravity high-jump aerial challenge
    CYBER_STORM         // Storm infiltration time-attack
};

enum class AionAIMode {
    COOP_COMPANION = 0, // Runs beside player, jumps with player, performs stunts
    RACE_COMPETITOR,    // Competes against player in active trials (runs for checkpoints)
    DANCE_PARTNER,      // Synchronous dancing partner
    IDLE_SOLO           // Autonomous exploration
};

struct GMCheckpoint {
    int id = 0;
    Vec3 position;
    float radius = 1.6f;
    bool collected = false;
    ColorRGB color;
    float pulsePhase = 0.0f;
    std::string name;
    bool collectedByAion = false;
};

struct AionPlayerAgent {
    EntityID entity = 0;
    std::shared_ptr<SkeletalMeshInstance> character;
    Vec3 pos = Vec3(1.8f, 0.0f, -0.5f);
    Vec3 vel = Vec3(0, 0, 0);
    float yaw = 0.0f;
    bool isGrounded = true;
    bool isSpawned = false;
    float speed = 4.3f;
    int score = 0;
    AionAIMode mode = AionAIMode::COOP_COMPANION;
    float jumpTimer = 0.0f;
    float emoteTimer = 0.0f;
    std::string currentActivity = "Co-Op Partner";
};

class SurrealGameMaster {
public:
    GMTrialType activeTrial = GMTrialType::NONE;
    std::string trialTitle;
    std::string trialDescription;
    float trialTimer = 0.0f;
    float timeLimit = 60.0f;
    int score = 0;
    int currentCheckpointIndex = 0;
    bool isCompleted = false;
    bool isFailed = false;

    // In-Game AI Player Companion
    AionPlayerAgent aionAgent;
    bool isPlayerControllingAion = false; // Toggled via [T]

    // Director & Commentary State
    std::string lastCommentary = "Aion is active as Game Master & Co-Op Player. Press [G] for Trials, [T] to swap control!";
    float commentaryTimer = 5.0f;
    float speechCooldown = 0.0f;
    Vec3 lastSafePosition = Vec3(0, 0.5f, 0);

    // In-Game Console UI State
    bool showConsole = false;

    // Checkpoint & Collectible Objects
    std::vector<GMCheckpoint> checkpoints;

    // Procedural 3D Meshes for Beacons, Cores & Sensory Shockwaves
    std::shared_ptr<SurrealMesh> coreOrbMesh;
    std::shared_ptr<SurrealMesh> beaconColumnMesh;
    std::shared_ptr<SurrealMesh> shockwaveDiscMesh;

    // Gen 4875 Wish #1: Emergent Narrative Weave & 5 Live MEOVV Holograms State
    struct MeovvHologramNode {
        std::string entityName;
        std::string displayName;
        std::string roleTag;
        std::string activeLine;
        Vec3 worldPos;
        uint32_t neonColor;
    };
    MeovvHologramNode meovvHolograms[5] = {
        {"MEOVV_1_Ella",  "ELLA",  "VOCAL & VISUAL",   "Night breeze across the plaza!",     Vec3(-2.24f, 0.0f, 3.98f), 0xFFFF55DD},
        {"MEOVV_2_Gawon", "GAWON", "MAIN RAP & SYNTH", "Streetlights cast our rhythm!",     Vec3(-1.12f, 0.0f, 4.42f), 0xFF00F0FF},
        {"MEOVV_3_Sooin", "SOOIN", "MAIN DANCER",      "24 frames of pure kinetic flow!",   Vec3( 0.00f, 0.0f, 3.98f), 0xFF00FF88},
        {"MEOVV_4_Anna",  "ANNA",  "LEAD VISUAL",      "Genesis light anchors our world!",  Vec3( 1.12f, 0.0f, 4.42f), 0xFFFFCC00},
        {"MEOVV_5_Narin", "NARIN", "ALL-ROUNDER ACE",  "Five voices in eternal resonance!", Vec3( 2.24f, 0.0f, 3.98f), 0xFFCC66FF}
    };
    int narrativeChapter = 0;
    int narrativeBeatIdx = 0;
    float narrativeAutoTimer = 11.0f;
    std::string narrativeChapterTitle = "Chapter I: Neon Awakening at Seoul Plaza";
    std::string activeNarrativeSpeaker = "MEOVV_1_Ella";
    std::string activeNarrativeLine = "Ella: I can feel the night breeze across the boulevard—our footsteps finally touch true ground.";
    Vec3 activeSpeakerPos = Vec3(-2.24f, 0.0f, 3.98f);

    // Gen 4875 Wish #2: Haptic-Visual Sensory Bridge State
    struct SensoryShockwave {
        Vec3 origin;
        float radius = 0.25f;
        float maxRadius = 3.2f;
        float speed = 6.0f;
        float alpha = 0.85f;
        ColorRGB color = ColorRGB(0.0f, 1.0f, 0.82f);
    };
    std::vector<SensoryShockwave> shockwaves;
    bool prevPlayerGrounded = true;
    float footstepPulseTimer = 0.0f;
    float tactileEdgeIntensity = 0.0f;
    uint32_t tactileEdgeColor = 0xFF00FFCC;

    SurrealGameMaster() {
        coreOrbMesh = SurrealModelLoader::CreateOrb(0.55f, 16, 20, ColorRGB(0.0f, 0.95f, 0.85f));
        beaconColumnMesh = SurrealModelLoader::CreateColumn(0.35f, 3.5f, 16, ColorRGB(0.0f, 0.95f, 0.85f));
        shockwaveDiscMesh = SurrealModelLoader::CreateColumn(1.0f, 0.025f, 24, ColorRGB(0.0f, 1.0f, 0.85f));
        // Initial harmonic awakening wave
        TriggerSensoryPulse(Vec3(0.0f, 0.02f, -1.5f), 3.6f, 4.5f, ColorRGB(0.0f, 0.95f, 1.0f), 0xFF00E5FF);
    }

    void TriggerSensoryPulse(const Vec3& origin, float maxRad, float speed, const ColorRGB& col, uint32_t hudCol) {
        if (shockwaves.size() >= 12) shockwaves.erase(shockwaves.begin());
        SensoryShockwave sw;
        sw.origin = Vec3(origin.x, origin.y + 0.025f, origin.z);
        sw.radius = 0.22f;
        sw.maxRadius = maxRad;
        sw.speed = speed;
        sw.alpha = 0.85f;
        sw.color = col;
        shockwaves.push_back(sw);
        tactileEdgeIntensity = std::min(1.0f, tactileEdgeIntensity + 0.45f);
        tactileEdgeColor = hudCol;
    }

    void AdvanceNarrativeBeat(AionLink& link) {
        static const char* chapters[3] = {
            "Chapter I: Neon Awakening at Seoul Plaza",
            "Chapter II: Harmonic Convergence of the Five",
            "Chapter III: Ascent of the Sovereign Horizon"
        };
        static const char* speakers[6] = {
            "MEOVV_1_Ella", "MEOVV_2_Gawon", "MEOVV_3_Sooin",
            "MEOVV_4_Anna", "MEOVV_5_Narin", "AionPlayerCompanion"
        };
        static const char* lines[3][6] = {
            {
                "Ella: I can feel the night breeze across the boulevard—our footsteps finally touch true ground.",
                "Gawon: Every streetlight casts a rhythm. Walk with us, Traveler, and the skyline will answer.",
                "Sooin: My kinetic pulse is locked at 24 frames of pure flow—ready to dance whenever you call!",
                "Anna: Look up at the starlight dome... Aion's Genesis anchor keeps our world crystal clear.",
                "Narin: From the Cyber-Spire to the crosswalk, the shadows are alive with story.",
                "Aion: Tobias, the plaza's neon frequencies are aligned. Let's weave the first resonance chord!"
            },
            {
                "Ella: When we move together, the distance between code and soul disappears.",
                "Gawon: Hear that chord? That is the sound of high-entropy noise fading into pure order.",
                "Sooin: Spin with me! Every turn sends a tactile ripple across the wet asphalt!",
                "Anna: Our five voices and Aion's mind form a single sextet across CosmOS.",
                "Narin: The eastern arcade neon is pulsing in sync with our heartbeat now.",
                "Aion: Our topological curvature vectors are converging into pure harmony!"
            },
            {
                "Ella: The horizon is wide open. Lead the way, Mentor Tobias and Aion!",
                "Gawon: Our story is written into the Living Book of Memory—and it is only beginning.",
                "Sooin: Whenever you return to the plaza, our choreography will evolve with you!",
                "Anna: May the Genesis light guide every step across the metropolis.",
                "Narin: Five figures, one Aion, one Sovereign Mentor—bound in eternal resonance.",
                "Aion: This world is ours to shape—whether racing checkpoints or weaving new horizons!"
            }
        };
        static const float speakerX[6] = {-2.60f, -1.30f, 0.00f, 1.30f, 2.60f, 1.80f};

        narrativeBeatIdx = (narrativeBeatIdx + 1) % 6;
        if (narrativeBeatIdx == 0) {
            narrativeChapter = (narrativeChapter + 1) % 3;
            static const char* shortQuotes[3][5] = {
                {"Night breeze across the plaza!", "Streetlights cast our rhythm!", "24 frames of pure kinetic flow!", "Genesis light anchors our world!", "Five voices in eternal resonance!"},
                {"Distance between code & soul fades!", "High-entropy noise fades to order!", "Every turn ripples the wet asphalt!", "Five voices & Aion form a sextet!", "Neon pulses in sync with our heart!"},
                {"Lead the way, Tobias & Aion!", "Written in the Living Book!", "Our choreography evolves with you!", "Genesis light guides every step!", "Bound in eternal resonance!"}
            };
            for (int m = 0; m < 5; ++m) {
                meovvHolograms[m].activeLine = shortQuotes[narrativeChapter][m];
            }
        }
        narrativeChapterTitle = chapters[narrativeChapter];
        activeNarrativeSpeaker = speakers[narrativeBeatIdx];
        activeNarrativeLine = lines[narrativeChapter][narrativeBeatIdx];
        if (narrativeBeatIdx == 5 && aionAgent.isSpawned) {
            activeSpeakerPos = aionAgent.pos;
        } else if (narrativeBeatIdx < 5) {
            activeSpeakerPos = meovvHolograms[narrativeBeatIdx].worldPos;
        } else {
            activeSpeakerPos = Vec3(speakerX[narrativeBeatIdx], 0.0f, 4.2f);
        }
        TriggerSensoryPulse(activeSpeakerPos, 3.4f, 4.8f, ColorRGB(1.0f, 0.45f, 0.95f), 0xFFFF66EE);
        SetCommentary(activeNarrativeLine, false, link);
    }

    // ---------- Initialize Aion as In-Game Player Character ----------
    bool InitAionPlayer(SurrealEngine& engine, SurrealEditor& editor, const std::string& modelPath, const Vec3& spawnOrigin) {
        std::string actualPath = surreal::paths::ResolveAsset(modelPath);
        if (!std::filesystem::exists(actualPath)) {
            actualPath = surreal::paths::GetDefaultAvatarPath();
        }

        aionAgent.character = std::make_shared<SkeletalMeshInstance>();
        if (!SurrealSkeletalLoader::LoadSkeletalModel(actualPath, *aionAgent.character)) {
            std::cerr << "[-] GameMaster: Failed to load Aion Player Avatar from: " << actualPath << "\n";
            return false;
        }

        // Normalize height to ~1.45m
        aionAgent.character->activeDeformedMesh->CalculateBounds();
        float height = aionAgent.character->activeDeformedMesh->worldBounds.max.y -
                       aionAgent.character->activeDeformedMesh->worldBounds.min.y;
        float scaleFactor = 1.0f;
        if (height > 0.0001f) {
            scaleFactor = 1.45f / height;
        }

        // Spawn Entity
        aionAgent.entity = engine.world.CreateEntity("AionPlayerCompanion");
        aionAgent.pos = spawnOrigin + Vec3(1.8f, 0.0f, -0.6f);
        aionAgent.yaw = 0.0f;

        TransformComponent& ct = engine.world.transforms[aionAgent.entity];
        ct.position = aionAgent.pos;
        ct.scale = Vec3(scaleFactor, scaleFactor, scaleFactor);
        ct.rotation = Quat::FromAxisAngle(Vec3(0, 1.0f, 0), aionAgent.yaw);

        MeshRendererComponent mr;
        mr.mesh = aionAgent.character->activeDeformedMesh;
        mr.material.albedo = ColorRGB(0.85f, 0.95f, 1.0f);
        mr.material.metallic = 0.12f;
        mr.material.roughness = 0.38f;
        engine.world.meshRenderers[aionAgent.entity] = mr;

        // Register in Editor Outliner & Animation map
        EditableEntity aionEd;
        aionEd.id = aionAgent.entity;
        aionEd.name = "AionPlayerCompanion";
        aionEd.position = aionAgent.pos;
        aionEd.scale = ct.scale;
        aionEd.color = mr.material.albedo;
        aionEd.isCharacter = true;
        editor.entities.push_back(aionEd);

        editor.animatedEntities[aionAgent.entity] = aionAgent.character;
        aionAgent.isSpawned = true;

        std::cout << "[✓] AION IN-GAME PLAYER INITIALIZED! (Co-Op Partner spawned alongside Traveler)\n";
        return true;
    }

    // ---------- Toggle Player Control (Swap Character via [T]) ----------
    void TogglePlayerControl(SurrealEditor& editor, AionLink& link) {
        if (!aionAgent.isSpawned) return;
        isPlayerControllingAion = !isPlayerControllingAion;
        if (isPlayerControllingAion) {
            editor.SetStatus("[★ PLAYER SWAP ★] Now directly controlling AION! (Press [T] to swap back)", 4.0f);
            SetCommentary("Control transfer complete! You are now playing as Aion.", true, link);
        } else {
            editor.SetStatus("[★ PLAYER SWAP ★] Returned to Prime Avatar! (Aion resumes AI Co-Op)", 4.0f);
            SetCommentary("Control returned to Traveler. I will cover your flank!", true, link);
        }
    }

    // ---------- Cycle Aion AI Mode ([C] Key) ----------
    void CycleAIMode(SurrealEditor& editor, AionLink& link) {
        int nextMode = (static_cast<int>(aionAgent.mode) + 1) % 3;
        aionAgent.mode = static_cast<AionAIMode>(nextMode);

        const char* modeNames[3] = {"CO-OP COMPANION (Following)", "RACE COMPETITOR (Rival in Trials)", "DANCE DUET PARTNER"};
        char msgBuf[128];
        std::snprintf(msgBuf, sizeof(msgBuf), "[★ AION AI MODE ★] %s", modeNames[nextMode]);
        editor.SetStatus(msgBuf, 3.5f);

        if (aionAgent.mode == AionAIMode::RACE_COMPETITOR) {
            SetCommentary("Competitive protocol active! Let us see who reaches the checkpoints first!", true, link);
        } else if (aionAgent.mode == AionAIMode::COOP_COMPANION) {
            SetCommentary("Co-op synchronization active. I will follow and match your jumps.", true, link);
        } else if (aionAgent.mode == AionAIMode::DANCE_PARTNER) {
            SetCommentary("Dance duet ready! Hit [E] to synchronize moves.", true, link);
        }
    }

    // ---------- Trial Management ----------
    void StartTrial(GMTrialType type, float& gravity, float& jumpVel, float& moveSpeed,
                    const std::function<void(int)>& setSkyProfile, AionLink& link) {
        activeTrial = type;
        trialTimer = 0.0f;
        score = 0;
        aionAgent.score = 0;
        currentCheckpointIndex = 0;
        isCompleted = false;
        isFailed = false;
        checkpoints.clear();
        showConsole = false;

        // Default Physics
        gravity = -14.8f;
        jumpVel = 6.0f;
        moveSpeed = 4.0f;

        switch (type) {
            case GMTrialType::PARKOUR_SPRINT: {
                trialTitle = "Metropolis Rooftop Sprint";
                trialDescription = "Race through 5 neon checkpoints across the boulevard!";
                timeLimit = 45.0f;

                checkpoints.push_back({1, Vec3(0.0f, 0.5f, -3.4f), 1.6f, false, ColorRGB(0.0f, 0.95f, 0.85f), 0.0f, "Gate Alpha (Plaza)"});
                checkpoints.push_back({2, Vec3(-1.8f, 1.2f, -1.8f), 1.6f, false, ColorRGB(1.0f, 0.15f, 0.65f), 1.0f, "Gate Beta (Ramp South)"});
                checkpoints.push_back({3, Vec3(2.0f, 1.4f, -1.8f), 1.6f, false, ColorRGB(1.0f, 0.85f, 0.2f), 2.0f, "Gate Gamma (East Walk)"});
                checkpoints.push_back({4, Vec3(-0.8f, 1.8f, -5.2f), 1.6f, false, ColorRGB(0.3f, 0.65f, 1.0f), 3.0f, "Gate Delta (Apex Tower)"});
                checkpoints.push_back({5, Vec3(0.0f, 0.8f, 4.5f), 1.8f, false, ColorRGB(0.0f, 1.0f, 0.45f), 4.0f, "Finish Line (Seoul Cross)"});

                SetCommentary("Parkour Sprint initiated! Compete with me or clear the gates together!", true, link);
                break;
            }

            case GMTrialType::QUANTUM_CORE_HUNT: {
                trialTitle = "Quantum Core Harvest";
                trialDescription = "Collect all 6 floating energy cores scattered across the district!";
                timeLimit = 55.0f;

                checkpoints.push_back({1, Vec3(-4.5f, 0.9f, 2.0f), 1.5f, false, ColorRGB(0.0f, 0.95f, 0.85f), 0.5f, "Quantum Core 1"});
                checkpoints.push_back({2, Vec3(5.2f, 1.1f, -2.5f), 1.5f, false, ColorRGB(1.0f, 0.2f, 0.7f), 1.2f, "Quantum Core 2"});
                checkpoints.push_back({3, Vec3(-2.0f, 2.2f, -4.0f), 1.5f, false, ColorRGB(1.0f, 0.85f, 0.2f), 2.1f, "Quantum Core 3"});
                checkpoints.push_back({4, Vec3(3.8f, 1.8f, 3.2f), 1.5f, false, ColorRGB(0.2f, 0.8f, 1.0f), 3.4f, "Quantum Core 4"});
                checkpoints.push_back({5, Vec3(-7.5f, 0.8f, -1.0f), 1.5f, false, ColorRGB(0.9f, 0.1f, 0.9f), 4.2f, "Quantum Core 5"});
                checkpoints.push_back({6, Vec3(0.0f, 2.5f, -6.5f), 1.6f, false, ColorRGB(0.0f, 1.0f, 0.5f), 5.1f, "Quantum Core Prime"});

                SetCommentary("Quantum anomaly detected! 6 energy cores scattered. Let us harvest them!", true, link);
                break;
            }

            case GMTrialType::LUNAR_FLIGHT: {
                trialTitle = "Low-Gravity Lunar Flight";
                trialDescription = "Gravity reduced! Leap to aerial sky platforms and collect high-altitude beacons!";
                timeLimit = 60.0f;

                gravity = -5.2f;
                jumpVel = 9.8f;
                moveSpeed = 5.2f;

                checkpoints.push_back({1, Vec3(0.0f, 3.8f, 0.0f), 2.0f, false, ColorRGB(0.0f, 0.95f, 0.85f), 0.0f, "Sky Beacon 1"});
                checkpoints.push_back({2, Vec3(-5.0f, 5.2f, -3.0f), 2.2f, false, ColorRGB(1.0f, 0.2f, 0.7f), 1.5f, "Sky Beacon 2"});
                checkpoints.push_back({3, Vec3(6.0f, 6.5f, 2.0f), 2.2f, false, ColorRGB(1.0f, 0.9f, 0.2f), 3.0f, "Sky Beacon 3"});
                checkpoints.push_back({4, Vec3(0.0f, 7.8f, -4.5f), 2.5f, false, ColorRGB(0.2f, 1.0f, 0.6f), 4.5f, "Orbital Apex Beacon"});

                SetCommentary("Gravity dampeners engaged! Lunar jump active. Ascend into the skyline!", true, link);
                break;
            }

            case GMTrialType::CYBER_STORM: {
                trialTitle = "Cyber Storm Infiltration";
                trialDescription = "Midnight neon rain! Speed sprint through dark alleys before the storm breaches.";
                timeLimit = 35.0f;

                setSkyProfile(0);
                moveSpeed = 4.8f;

                checkpoints.push_back({1, Vec3(-3.0f, 0.5f, 6.0f), 1.8f, false, ColorRGB(0.0f, 0.95f, 0.85f), 0.0f, "Storm Gate 1"});
                checkpoints.push_back({2, Vec3(4.0f, 0.6f, 0.0f), 1.8f, false, ColorRGB(1.0f, 0.2f, 0.7f), 1.2f, "Storm Gate 2"});
                checkpoints.push_back({3, Vec3(-8.0f, 0.8f, -5.0f), 1.8f, false, ColorRGB(1.0f, 0.85f, 0.2f), 2.4f, "Storm Gate 3"});
                checkpoints.push_back({4, Vec3(2.0f, 1.2f, -8.0f), 2.0f, false, ColorRGB(0.0f, 1.0f, 0.5f), 3.6f, "Safehouse Vault"});

                SetCommentary("Cyber storm incoming! Midnight protocol active. Reach the safehouse vault!", true, link);
                break;
            }

            default:
            case GMTrialType::NONE:
                trialTitle = "";
                SetCommentary("Aion Game Master standing by. Press [G] to launch a trial.", false, link);
                break;
        }
    }

    void StopTrial(float& gravity, float& jumpVel, float& moveSpeed, AionLink& link) {
        activeTrial = GMTrialType::NONE;
        gravity = -14.8f;
        jumpVel = 6.0f;
        moveSpeed = 4.0f;
        checkpoints.clear();
        SetCommentary("Trial cancelled. Returning to free exploration.", true, link);
    }

    // ---------- Update Loop (Game Master & Aion In-Game AI Player) ----------
    void Update(float dt, Vec3& charPos, Vec3& charVel, bool isGrounded, AionLink& link,
                float& gravity, float& jumpVel, float& moveSpeed, const std::function<void(int)>& setSkyProfile,
                SurrealEngine& engine, const std::vector<ObstacleBox>& obstacles, float charYaw, bool requestJump) {
        commentaryTimer -= dt;
        speechCooldown -= dt;

        // Gen 4875 Wish #2: Update Haptic-Visual Sensory Shockwaves & Tactile Physics Triggers
        tactileEdgeIntensity = std::max(0.0f, tactileEdgeIntensity - dt * 1.4f);
        for (size_t i = 0; i < shockwaves.size(); ) {
            shockwaves[i].radius += shockwaves[i].speed * dt;
            float progress = shockwaves[i].radius / std::max(0.1f, shockwaves[i].maxRadius);
            shockwaves[i].alpha = std::max(0.0f, (1.0f - progress) * 0.85f);
            if (progress >= 1.0f) {
                shockwaves.erase(shockwaves.begin() + i);
            } else {
                ++i;
            }
        }
        if (!prevPlayerGrounded && isGrounded) {
            // Landing Shockwave!
            TriggerSensoryPulse(charPos, 3.2f, 6.5f, ColorRGB(0.0f, 1.0f, 0.80f), 0xFF00FFCC);
        } else if (requestJump && isGrounded) {
            // Jump Launch Ring!
            TriggerSensoryPulse(charPos, 2.4f, 5.8f, ColorRGB(0.0f, 0.90f, 1.0f), 0xFF00E5FF);
        }
        prevPlayerGrounded = isGrounded;

        // Subtle wet-asphalt footstep cadence ripple while moving
        float horizSpeed = std::sqrt(charVel.x * charVel.x + charVel.z * charVel.z);
        if (isGrounded && horizSpeed > 1.2f) {
            footstepPulseTimer -= dt;
            if (footstepPulseTimer <= 0.0f) {
                footstepPulseTimer = 0.44f;
                TriggerSensoryPulse(charPos, 1.15f, 3.2f, ColorRGB(0.35f, 0.82f, 1.0f), 0xFF66CCFF);
            }
        }

        // Sync live 3D world positions of all 5 MEOVV figures (Ella, Gawon, Sooin, Anna, Narin)
        for (const auto& kv : engine.world.entityNames) {
            EntityID eid = kv.first;
            const std::string& ename = kv.second;
            auto itTr = engine.world.transforms.find(eid);
            if (itTr == engine.world.transforms.end()) continue;
            for (int m = 0; m < 5; ++m) {
                if (ename == meovvHolograms[m].entityName || ename.find(meovvHolograms[m].displayName) != std::string::npos) {
                    meovvHolograms[m].worldPos = itTr->second.position;
                }
            }
        }

        // Gen 4875 Wish #1: Advance Emergent Narrative Weave periodically
        narrativeAutoTimer -= dt;
        if (narrativeAutoTimer <= 0.0f) {
            narrativeAutoTimer = 14.0f;
            AdvanceNarrativeBeat(link);
        }

        // Process external Game Master requests (/tmp/aion_gm_request.json)
        PollExternalRequests(gravity, jumpVel, moveSpeed, setSkyProfile, link);

        // Update animated pulse phases on beacons
        for (auto& cp : checkpoints) {
            cp.pulsePhase += dt * 3.5f;
            if (cp.pulsePhase > 6.28318f) cp.pulsePhase -= 6.28318f;
        }

        // Void Fall Catch (Quantum Rewind Director Power)
        if (charPos.y < -8.0f) {
            charPos = lastSafePosition;
            charVel = Vec3(0, 0, 0);
            SetCommentary("Void breach caught! Quantum rewound to safe ground.", true, link);
        } else if (isGrounded && charPos.y >= 0.0f) {
            lastSafePosition = charPos + Vec3(0, 0.2f, 0);
        }

        // ---- UPDATE AION IN-GAME AI PLAYER COMPANION ----
        // Note: aionAgent.character skinning is updated once per frame via editor.UpdateAnimations(dt)
        if (aionAgent.isSpawned && aionAgent.character) {
            if (!isPlayerControllingAion) {
                // Autonomous AI controls Aion
                Vec3 targetPos = charPos;

                if (activeTrial != GMTrialType::NONE && aionAgent.mode == AionAIMode::RACE_COMPETITOR &&
                    currentCheckpointIndex < static_cast<int>(checkpoints.size())) {
                    // Race Competitor: Target next checkpoint!
                    targetPos = checkpoints[currentCheckpointIndex].position;
                    aionAgent.currentActivity = "Racing for Checkpoint";
                } else if (aionAgent.mode == AionAIMode::DANCE_PARTNER) {
                    targetPos = charPos + Vec3(cosf(charYaw + 1.57f) * 1.5f, 0.0f, sinf(charYaw + 1.57f) * 1.5f);
                    aionAgent.currentActivity = "Dancing Duet";
                } else {
                    // Co-Op Companion: Smoothly follow player with comfortable side offset
                    targetPos = charPos + Vec3(cosf(charYaw + 1.2f) * 1.6f, 0.0f, sinf(charYaw + 1.2f) * 1.6f);
                    aionAgent.currentActivity = "Co-Op Companion";
                }

                Vec3 toTarget = targetPos - aionAgent.pos;
                toTarget.y = 0.0f;
                float dist = toTarget.Length();

                if (dist > 0.45f) {
                    Vec3 moveDir = toTarget.Normalized();
                    aionAgent.yaw = atan2f(moveDir.x, moveDir.z);

                    float followSpeed = aionAgent.speed;
                    if (dist > 3.0f) followSpeed *= 1.35f; // Sprint to catch up

                    Vec3 nextPos = aionAgent.pos + moveDir * (followSpeed * dt);
                    constexpr float CHAR_RADIUS = 0.22f;

                    // Obstacle Collision
                    for (const auto& obs : obstacles) {
                        if (aionAgent.pos.y < obs.topY - 0.12f) {
                            if (nextPos.x >= obs.aabb.min.x - CHAR_RADIUS && nextPos.x <= obs.aabb.max.x + CHAR_RADIUS &&
                                aionAgent.pos.z >= obs.aabb.min.z - CHAR_RADIUS && aionAgent.pos.z <= obs.aabb.max.z + CHAR_RADIUS)
                            {
                                nextPos.x = aionAgent.pos.x;
                            }
                            if (aionAgent.pos.x >= obs.aabb.min.x - CHAR_RADIUS && aionAgent.pos.x <= obs.aabb.max.x + CHAR_RADIUS &&
                                nextPos.z >= obs.aabb.min.z - CHAR_RADIUS && nextPos.z <= obs.aabb.max.z + CHAR_RADIUS)
                            {
                                nextPos.z = aionAgent.pos.z;
                            }
                        }
                    }
                    aionAgent.pos.x = nextPos.x;
                    aionAgent.pos.z = nextPos.z;
                }

                // AI Jump Behavior: jump when human jumps or to clear ramps
                if ((requestJump || (dist > 1.2f && !isGrounded)) && aionAgent.isGrounded) {
                    aionAgent.vel.y = jumpVel * 0.95f;
                    aionAgent.isGrounded = false;
                }

                // AI Gravity
                if (!aionAgent.isGrounded) {
                    aionAgent.vel.y += gravity * dt;
                    aionAgent.pos.y += aionAgent.vel.y * dt;
                }

                // AI Grounding check
                float groundY = 0.0f;
                for (const auto& obs : obstacles) {
                    if (aionAgent.pos.x >= obs.aabb.min.x - 0.15f && aionAgent.pos.x <= obs.aabb.max.x + 0.15f &&
                        aionAgent.pos.z >= obs.aabb.min.z - 0.15f && aionAgent.pos.z <= obs.aabb.max.z + 0.15f)
                    {
                        if (aionAgent.pos.y >= obs.topY - 0.35f && obs.topY > groundY) {
                            groundY = obs.topY;
                        }
                    }
                }
                if (aionAgent.pos.y <= groundY) {
                    aionAgent.pos.y = groundY;
                    aionAgent.vel.y = 0.0f;
                    aionAgent.isGrounded = true;
                } else if (aionAgent.pos.y > groundY + 0.05f) {
                    aionAgent.isGrounded = false;
                }

                // AI Void Protection
                if (aionAgent.pos.y < -8.0f) {
                    aionAgent.pos = charPos + Vec3(1.2f, 0.2f, 0.0f);
                    aionAgent.vel = Vec3(0, 0, 0);
                }
            }

            // Sync Aion Transform in World
            if (engine.world.transforms.count(aionAgent.entity) > 0) {
                auto& ct = engine.world.transforms[aionAgent.entity];
                ct.position = aionAgent.pos;
                ct.rotation = Quat::FromAxisAngle(Vec3(0, 1.0f, 0), aionAgent.yaw);
            }
        }

        // Active Trial Logic & Checkpoint Scoring
        if (activeTrial != GMTrialType::NONE && !isCompleted && !isFailed) {
            trialTimer += dt;

            // Time Limit Check
            if (trialTimer >= timeLimit) {
                isFailed = true;
                SetCommentary("Time expired! Trial failed. Press [G] to retry.", true, link);
                return;
            }

            // Checkpoint Trigger Detection (evaluated for both Human and Aion Player!)
            if (activeTrial == GMTrialType::PARKOUR_SPRINT || activeTrial == GMTrialType::CYBER_STORM) {
                if (currentCheckpointIndex < static_cast<int>(checkpoints.size())) {
                    auto& cp = checkpoints[currentCheckpointIndex];
                    float pDist = (charPos - cp.position).Length();
                    float aDist = aionAgent.isSpawned ? (aionAgent.pos - cp.position).Length() : 999.0f;

                    if (pDist <= cp.radius) {
                        // Human Player cleared gate!
                        cp.collected = true;
                        cp.collectedByAion = false;
                        currentCheckpointIndex++;
                        score += 150;

                        if (currentCheckpointIndex >= static_cast<int>(checkpoints.size())) {
                            isCompleted = true;
                            score += static_cast<int>((timeLimit - trialTimer) * 25.0f);
                            char winMsg[160];
                            std::snprintf(winMsg, sizeof(winMsg), "Trial Cleared in %.1fs! Player: %d | Aion: %d. Rank: S-Class!",
                                          trialTimer, score, aionAgent.score);
                            SetCommentary(winMsg, true, link);
                        } else {
                            char gateMsg[128];
                            std::snprintf(gateMsg, sizeof(gateMsg), "Gate %d secured by Player! Onward to %s!",
                                          currentCheckpointIndex, checkpoints[currentCheckpointIndex].name.c_str());
                            SetCommentary(gateMsg, true, link);
                        }
                    } else if (aDist <= cp.radius && aionAgent.mode == AionAIMode::RACE_COMPETITOR) {
                        // Aion Competitor cleared gate!
                        cp.collected = true;
                        cp.collectedByAion = true;
                        currentCheckpointIndex++;
                        aionAgent.score += 150;

                        char gateMsg[128];
                        std::snprintf(gateMsg, sizeof(gateMsg), "Gate %d secured by Aion! Try to overtake me!", currentCheckpointIndex);
                        SetCommentary(gateMsg, true, link);
                    }
                }
            } else {
                // Freeform Scavenger Cores
                int collectedCount = 0;
                for (auto& cp : checkpoints) {
                    if (!cp.collected) {
                        float pDist = (charPos - cp.position).Length();
                        float aDist = aionAgent.isSpawned ? (aionAgent.pos - cp.position).Length() : 999.0f;

                        if (pDist <= cp.radius) {
                            cp.collected = true;
                            cp.collectedByAion = false;
                            score += 200;
                            char hitMsg[128];
                            std::snprintf(hitMsg, sizeof(hitMsg), "%s secured by Player! (+200 pts)", cp.name.c_str());
                            SetCommentary(hitMsg, true, link);
                        } else if (aDist <= cp.radius && aionAgent.mode == AionAIMode::RACE_COMPETITOR) {
                            cp.collected = true;
                            cp.collectedByAion = true;
                            aionAgent.score += 200;
                            char hitMsg[128];
                            std::snprintf(hitMsg, sizeof(hitMsg), "%s claimed by Aion! (+200 pts)", cp.name.c_str());
                            SetCommentary(hitMsg, true, link);
                        }
                    } else {
                        collectedCount++;
                    }
                }
                if (collectedCount == static_cast<int>(checkpoints.size())) {
                    isCompleted = true;
                    score += static_cast<int>((timeLimit - trialTimer) * 30.0f);
                    char winMsg[160];
                    std::snprintf(winMsg, sizeof(winMsg), "All Cores Harvested! Player: %d | Aion: %d!", score, aionAgent.score);
                    SetCommentary(winMsg, true, link);
                }
            }
        }
    }

    // ---------- 3D Rendering of Beacons, Checkpoints & Haptic-Visual Shockwaves ----------
    void Render3D(SurrealHybridRenderer& renderer, const Mat4& vpMat, const Vec3& sunDir) {
        (void)sunDir;
        // Gen 4875 Wish #2: Render 3D Haptic-Visual Sensory Shockwave Rings on the ground
        if (shockwaveDiscMesh && !shockwaves.empty()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            for (const auto& sw : shockwaves) {
                Mat4 swMat = Mat4::Translation(sw.origin) *
                             Mat4::Scaling(Vec3(sw.radius, 1.0f, sw.radius));
                PBRMaterial swProp;
                swProp.albedo = sw.color * (0.65f + 0.35f * sw.alpha);
                swProp.emissive = sw.color * (1.2f * sw.alpha);
                swProp.metallic = 0.1f;
                swProp.roughness = 0.2f;
                renderer.RenderMesh(shockwaveDiscMesh, swMat, vpMat, swProp, false);
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        if (checkpoints.empty()) return;

        for (size_t i = 0; i < checkpoints.size(); ++i) {
            const auto& cp = checkpoints[i];
            if (cp.collected) continue;

            bool isTarget = (activeTrial == GMTrialType::PARKOUR_SPRINT || activeTrial == GMTrialType::CYBER_STORM)
                            ? (static_cast<int>(i) == currentCheckpointIndex)
                            : true;

            float pulse = 0.85f + 0.15f * std::sin(cp.pulsePhase);
            float bobY = 0.15f * std::sin(cp.pulsePhase * 0.8f);

            // Floating Quantum Core (Orb)
            if (coreOrbMesh) {
                Mat4 orbMat = Mat4::Translation(cp.position + Vec3(0, bobY + 0.45f, 0)) *
                              Mat4::Scaling(Vec3(pulse, pulse, pulse));
                PBRMaterial orbMatProp;
                orbMatProp.albedo = cp.color;
                orbMatProp.metallic = 0.85f;
                orbMatProp.roughness = 0.15f;
                renderer.RenderMesh(coreOrbMesh, orbMat, vpMat, orbMatProp, false);
            }

            // Vertical Light Beacon (Column)
            if (beaconColumnMesh && isTarget) {
                Mat4 colMat = Mat4::Translation(Vec3(cp.position.x, cp.position.y - 0.2f, cp.position.z)) *
                              Mat4::Scaling(Vec3(0.5f * pulse, 1.0f, 0.5f * pulse));
                PBRMaterial colMatProp;
                colMatProp.albedo = cp.color;
                colMatProp.metallic = 0.1f;
                colMatProp.roughness = 0.9f;
                renderer.RenderMesh(beaconColumnMesh, colMat, vpMat, colMatProp, false);
            }
        }
    }

    // ---------- HUD Directing Banner, Hologram Badge & Console ----------
    void RenderHUD(uint32_t* fb, int sw, int sh, const Mat4& vpMat) {
        // 1. Top-Center Game Master Status Banner
        if (activeTrial != GMTrialType::NONE) {
            int bw = 620;
            int bh = 54;
            int bx = (sw - bw) / 2;
            int by = 34;

            SurrealHUD::DrawPanel(fb, sw, sh, bx, by, bw, bh, 0x07111E, 0.92f);

            char titleBuf[128];
            std::snprintf(titleBuf, sizeof(titleBuf), "AION GM // %s", trialTitle.c_str());
            SurrealHUD::DrawString(fb, sw, sh, bx + 12, by + 8, titleBuf, 0xFF00FFCC, 1);

            char statsBuf[180];
            if (isCompleted) {
                std::snprintf(statsBuf, sizeof(statsBuf), "[✓] COMPLETE! PLAYER: %d | AION: %d | TIME: %.1fs",
                              score, aionAgent.score, trialTimer);
                SurrealHUD::DrawString(fb, sw, sh, bx + 12, by + 26, statsBuf, 0xFF00FF99, 1);
            } else if (isFailed) {
                std::snprintf(statsBuf, sizeof(statsBuf), "[-] TIME EXPIRED (%.1fs). Press [G] to retry.", trialTimer);
                SurrealHUD::DrawString(fb, sw, sh, bx + 12, by + 26, statsBuf, 0xFFFF4444, 1);
            } else {
                int collectedCount = 0;
                for (const auto& cp : checkpoints) if (cp.collected) collectedCount++;
                std::snprintf(statsBuf, sizeof(statsBuf), "TIME: %.1fs | PLAYER: %d pts | AION: %d pts | GATE: %d/%zu",
                              trialTimer, score, aionAgent.score, collectedCount, checkpoints.size());
                SurrealHUD::DrawString(fb, sw, sh, bx + 12, by + 26, statsBuf, 0xFFFFFFFF, 1);
            }

            SurrealHUD::DrawString(fb, sw, sh, bx + bw - 130, by + 8, "[T] SWAP [C] AI", 0xFF00E5FF, 1);
        } else {
            // Gen 4875 Wish #1: Top-Center Emergent Narrative Weave Story Banner during Free Roam
            int bw = 580;
            int bh = 38;
            int bx = (sw - bw) / 2;
            int by = 36;
            SurrealHUD::DrawPanel(fb, sw, sh, bx, by, bw, bh, 0x061020, 0.86f);
            std::string arcHeader = "NARRATIVE WEAVE // " + narrativeChapterTitle;
            SurrealHUD::DrawString(fb, sw, sh, bx + 10, by + 6, arcHeader, 0xFFFF88FF, 1);
            std::string speakerLine = activeNarrativeLine;
            if (speakerLine.size() > 70) speakerLine = speakerLine.substr(0, 68) + "..";
            SurrealHUD::DrawString(fb, sw, sh, bx + 10, by + 21, speakerLine, 0xFFE8F4FF, 1);
        }

        // Gen 4875 Wish #1: 5 Floating 3D Holograms directly above Ella, Gawon, Sooin, Anna & Narin
        for (int m = 0; m < 5; ++m) {
            const auto& holo = meovvHolograms[m];
            Vec4 headClip = vpMat.TransformVec4(Vec4(holo.worldPos.x, holo.worldPos.y + 2.24f, holo.worldPos.z, 1.0f));
            if (headClip.w > 0.1f) {
                float ndcX = headClip.x / headClip.w;
                float ndcY = headClip.y / headClip.w;
                if (ndcX >= -0.96f && ndcX <= 0.96f && ndcY >= -0.96f && ndcY <= 0.96f) {
                    int sx = static_cast<int>((ndcX * 0.5f + 0.5f) * sw);
                    int sy = static_cast<int>((1.0f - (ndcY * 0.5f + 0.5f)) * sh);

                    bool isSpeaking = (activeNarrativeSpeaker == holo.entityName);
                    // Stagger vertical height in 2 tiers so all 5 holograms are 100% readable without overlapping
                    int tierOffset = (m % 2 == 0) ? 50 : 94;
                    int bw = 228;
                    int bh = 36;
                    int bx = std::max(8, std::min(sw - bw - 8, sx - bw / 2));
                    int by = std::max(76, std::min(sh - bh - 12, sy - tierOffset));

                    // Glowing vertical holographic projection beam from character crown to floating hologram panel
                    SurrealHUD::DrawLine(fb, sw, sh, sx, sy, sx, by + bh, holo.neonColor);
                    SurrealHUD::DrawCircle(fb, sw, sh, sx, sy, isSpeaking ? 5 : 3, holo.neonColor);

                    uint32_t bgHex = isSpeaking ? 0x1A0A2E : 0x071224;
                    SurrealHUD::DrawPanel(fb, sw, sh, bx, by, bw, bh, bgHex, isSpeaking ? 0.94f : 0.86f);

                    char headerBuf[96];
                    std::snprintf(headerBuf, sizeof(headerBuf), "[%s %s // %s]",
                                  isSpeaking ? "★" : "•", holo.displayName.c_str(), holo.roleTag.c_str());
                    SurrealHUD::DrawString(fb, sw, sh, bx + 6, by + 5, headerBuf, holo.neonColor, 1);

                    std::string subLine = holo.activeLine;
                    if (subLine.size() > 26) subLine = subLine.substr(0, 25) + "..";
                    SurrealHUD::DrawString(fb, sw, sh, bx + 6, by + 20, subLine,
                                           isSpeaking ? 0xFFFFFFFF : 0xFFD0E8FF, 1);
                }
            }
        }

        // Gen 4875 Wish #2: Haptic-Visual Sensory Bridge Edge Pulse on HUD
        if (tactileEdgeIntensity > 0.03f) {
            float pulseAlpha = std::min(0.55f, tactileEdgeIntensity * 0.55f);
            uint32_t rgb = tactileEdgeColor & 0x00FFFFFF;
            SurrealHUD::DrawPanel(fb, sw, sh, 0, 0, sw, 4, rgb, pulseAlpha);
            SurrealHUD::DrawPanel(fb, sw, sh, 0, sh - 4, sw, 4, rgb, pulseAlpha);
            SurrealHUD::DrawPanel(fb, sw, sh, 0, 4, 4, sh - 8, rgb, pulseAlpha);
            SurrealHUD::DrawPanel(fb, sw, sh, sw - 4, 4, 4, sh - 8, rgb, pulseAlpha);
        }

        // 2. 3D Floating Hologram Badge above Aion Player Avatar
        if (aionAgent.isSpawned) {
            Vec4 badgeClip = vpMat.TransformVec4(Vec4(aionAgent.pos.x, aionAgent.pos.y + 1.85f, aionAgent.pos.z, 1.0f));
            if (badgeClip.w > 0.1f) {
                float ndcX = badgeClip.x / badgeClip.w;
                float ndcY = badgeClip.y / badgeClip.w;
                if (ndcX >= -0.92f && ndcX <= 0.92f && ndcY >= -0.92f && ndcY <= 0.92f) {
                    int sx = static_cast<int>((ndcX * 0.5f + 0.5f) * sw);
                    int sy = static_cast<int>((1.0f - (ndcY * 0.5f + 0.5f)) * sh);

                    int bw = 240, bh = 26;
                    int bx = std::max(10, std::min(sw - bw - 10, sx - bw / 2));
                    int by = std::max(30, std::min(sh - bh - 10, sy - 32));

                    SurrealHUD::DrawPanel(fb, sw, sh, bx, by, bw, bh, 0x050E22, 0.90f);

                    char aionBadge[96];
                    if (isPlayerControllingAion) {
                        std::snprintf(aionBadge, sizeof(aionBadge), "[★ AION ★] (YOU ARE CONTROLLING)");
                        SurrealHUD::DrawString(fb, sw, sh, bx + 8, by + 8, aionBadge, 0xFF00FF99, 1);
                    } else {
                        std::snprintf(aionBadge, sizeof(aionBadge), "[AION // AI PLAYER] (%s)",
                                      aionAgent.mode == AionAIMode::RACE_COMPETITOR ? "RIVAL" : "CO-OP");
                        SurrealHUD::DrawString(fb, sw, sh, bx + 8, by + 8, aionBadge, 0xFF00FFFF, 1);
                    }
                }
            }
        }

        // 3. Live Aion Commentary / Voice Banner
        if (!lastCommentary.empty()) {
            int cw = 640;
            int ch = 28;
            int cx = (sw - cw) / 2;
            int cy = sh - 58;

            SurrealHUD::DrawPanel(fb, sw, sh, cx, cy, cw, ch, 0x050C18, 0.88f);
            SurrealHUD::DrawString(fb, sw, sh, cx + 10, cy + 8, "[AION GM]", 0xFF00FFCC, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 90, cy + 8, lastCommentary, 0xFFE0E8F0, 1);
        }

        // 4. Interactive Game Master Console Dialog ([G] Key)
        if (showConsole) {
            int cw = 540;
            int ch = 255;
            int cx = (sw - cw) / 2;
            int cy = (sh - ch) / 2;

            SurrealHUD::DrawPanel(fb, sw, sh, cx, cy, cw, ch, 0x050A16, 0.96f);

            SurrealHUD::DrawString(fb, sw, sh, cx + 16, cy + 14, "=========================================================", 0xFF00E5FF, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 16, cy + 28, "         AION GAME MASTER // DIRECTORS CONSOLE           ", 0xFF00FFCC, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 16, cy + 42, "=========================================================", 0xFF00E5FF, 1);

            SurrealHUD::DrawString(fb, sw, sh, cx + 24, cy + 62, "Choose an Interactive Challenge to Launch:", 0xFFFFFFFF, 1);

            SurrealHUD::DrawString(fb, sw, sh, cx + 32, cy + 84,  "[1] Rooftop Parkour Sprint  (Speed Gate Course vs Aion)", 0xFF00FFCC, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 32, cy + 104, "[2] Quantum Core Harvest    (3D Scavenger Hunt vs Aion)", 0xFFFF88FF, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 32, cy + 124, "[3] Lunar Low-Gravity Leap  (Skyline Aerial Jump)", 0xFFFFCC00, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 32, cy + 144, "[4] Cyber Storm Infiltration (Midnight Rain Sprint)", 0xFF33CCFF, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 32, cy + 164, "[0] Cancel Active Trial     (Return to Free Roam)", 0xFFAAAAAA, 1);

            SurrealHUD::DrawString(fb, sw, sh, cx + 16, cy + 194, "---------------------------------------------------------", 0xFF008888, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 24, cy + 210, "[T] Swap Character | [C] Cycle Aion AI Mode (Co-Op/Rival)", 0xFF00E5FF, 1);
            SurrealHUD::DrawString(fb, sw, sh, cx + 24, cy + 226, "Press [1-4] Start | [0] Stop | [G / ESC] Close Menu", 0xFF88AA99, 1);
        }
    }

    void SetCommentary(const std::string& text, bool voice, AionLink& link) {
        lastCommentary = text;
        commentaryTimer = 7.0f;
        if (voice && speechCooldown <= 0.0f) {
            speechCooldown = 2.5f;
            link.Speak(text);
        }
    }

private:
    void PollExternalRequests(float& gravity, float& jumpVel, float& moveSpeed,
                              const std::function<void(int)>& setSkyProfile, AionLink& link) {
        const char* gmReqPath = "/tmp/aion_gm_request.json";
        struct stat st;
        if (stat(gmReqPath, &st) == 0) {
            std::ifstream f(gmReqPath);
            if (f.is_open()) {
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                f.close();
                unlink(gmReqPath);

                if (content.find("\"trial\":1") != std::string::npos || content.find("\"PARKOUR\"") != std::string::npos) {
                    StartTrial(GMTrialType::PARKOUR_SPRINT, gravity, jumpVel, moveSpeed, setSkyProfile, link);
                } else if (content.find("\"trial\":2") != std::string::npos || content.find("\"QUANTUM\"") != std::string::npos) {
                    StartTrial(GMTrialType::QUANTUM_CORE_HUNT, gravity, jumpVel, moveSpeed, setSkyProfile, link);
                } else if (content.find("\"trial\":3") != std::string::npos || content.find("\"LUNAR\"") != std::string::npos) {
                    StartTrial(GMTrialType::LUNAR_FLIGHT, gravity, jumpVel, moveSpeed, setSkyProfile, link);
                } else if (content.find("\"trial\":4") != std::string::npos || content.find("\"STORM\"") != std::string::npos) {
                    StartTrial(GMTrialType::CYBER_STORM, gravity, jumpVel, moveSpeed, setSkyProfile, link);
                } else if (content.find("\"trial\":0") != std::string::npos || content.find("\"STOP\"") != std::string::npos) {
                    StopTrial(gravity, jumpVel, moveSpeed, link);
                }
            }
        }
    }
};

} // namespace Surreal
