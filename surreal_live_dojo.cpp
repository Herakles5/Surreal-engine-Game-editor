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
#include <iostream>
#include <vector>
#include <chrono>

int main(int argc, char* argv[]) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--benchmark-headless") {
            headless = true;
        }
    }

    std::cout << "=========================================================\n";
    std::cout << "  AION SURREAL 3D ENGINE - 3D GAME STUDIO & LIVE DOJO\n";
    std::cout << "  CPU + GPU Hybrid Sovereign Engine for CosmOS & Linux\n";
    std::cout << "=========================================================\n";

    constexpr uint32_t WIN_WIDTH = 960;
    constexpr uint32_t WIN_HEIGHT = 640;

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
                WIN_WIDTH, WIN_HEIGHT,
                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
            );
            if (!window) {
                std::cerr << "[-] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
                headless = true;
            } else {
                hybridRenderer.Init(window, WIN_WIDTH, WIN_HEIGHT);
            }
        }
    }

    // 1. Initialize Engine & CPU Framebuffers
    std::vector<uint32_t> framebuffer(WIN_WIDTH * WIN_HEIGHT, 0xFF050812);
    std::vector<uint32_t> hudFramebuffer(WIN_WIDTH * WIN_HEIGHT, 0x00000000);
    Surreal::SurrealEngine engine(WIN_WIDTH, WIN_HEIGHT);
    engine.AttachFramebuffer(framebuffer.data(), WIN_WIDTH * sizeof(uint32_t));

    // 2. Build 3D Terrain & Obstacle Parkour Environment
    std::cout << "[*] Constructing 3D Cyberpunk Grid Terrain & Parkour Platforms...\n";

    // 2a. Ground Grid Floor (24m x 24m)
    Surreal::EntityID floorEntity = engine.world.CreateEntity("CyberGridFloor");
    Surreal::MeshRendererComponent floorRenderer;
    floorRenderer.mesh = Surreal::SurrealTerrainBuilder::CreateCyberGridFloor(24.0f, 24);
    floorRenderer.material.albedo = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
    floorRenderer.material.metallic = 0.20f;
    floorRenderer.material.roughness = 0.55f;
    engine.world.meshRenderers[floorEntity] = floorRenderer;

    // 2b. Obstacle Platforms
    std::vector<Surreal::ObstacleBox> obstacles;
    obstacles.emplace_back(Surreal::Vec3(-1.8f, 0.25f, -1.8f), Surreal::Vec3(0.9f, 0.25f, 0.9f), "Platform_Low", Surreal::ColorRGB(0.0f, 0.95f, 0.85f));
    obstacles.emplace_back(Surreal::Vec3(0.0f, 0.475f, -3.4f), Surreal::Vec3(1.1f, 0.475f, 1.1f), "Platform_Mid", Surreal::ColorRGB(1.0f, 0.15f, 0.65f));
    obstacles.emplace_back(Surreal::Vec3(2.0f, 0.70f, -1.8f), Surreal::Vec3(0.9f, 0.70f, 0.9f), "Platform_High", Surreal::ColorRGB(1.0f, 0.85f, 0.2f));
    obstacles.emplace_back(Surreal::Vec3(-0.8f, 0.925f, -5.2f), Surreal::Vec3(1.0f, 0.925f, 1.0f), "Platform_Apex", Surreal::ColorRGB(0.3f, 0.65f, 1.0f));

    // Register platforms in world ECS
    std::vector<Surreal::EntityID> platformEntities;
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

    // Register entities in Editor Outliner
    Surreal::EditableEntity floorEd;
    floorEd.id = floorEntity;
    floorEd.name = "CyberGridFloor";
    floorEd.position = Surreal::Vec3(0, 0, 0);
    floorEd.scale = Surreal::Vec3(24.0f, 0.1f, 24.0f);
    floorEd.color = Surreal::ColorRGB(1.0f, 1.0f, 1.0f);
    floorEd.metallic = 0.20f;
    floorEd.roughness = 0.55f;
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
    editor.selectedIndex = 2; // Default select Platform_Low

    // 5. Initialize Camera Controllers
    Surreal::InteractiveCameraController playCamera;
    playCamera.mode = Surreal::CameraMode::ORBIT_THIRD_PERSON;
    playCamera.distance = 2.8f;
    playCamera.targetDistance = 2.8f;
    playCamera.pitch = 0.28f;
    playCamera.yaw = 0.0f;
    playCamera.SetTarget(Surreal::Vec3(0, 1.25f, 0));

    // Setup Atmospheric PBR Lighting
    engine.world.sunLight.direction = Surreal::Vec3(0.55f, 0.75f, 0.45f).Normalized();
    engine.world.sunLight.color = Surreal::ColorRGB(1.0f, 0.98f, 0.94f);
    engine.world.sunLight.intensity = 1.35f;
    engine.world.ambientLight = Surreal::ColorRGB(0.32f, 0.32f, 0.38f);

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

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        dt = std::min(dt, 0.05f);

        editor.Update(dt);

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
                        if (editor.showModelCatalog) {
                            editor.showModelCatalog = false;
                            editor.SetStatus("[!] Model catalog closed.");
                        } else if (editor.isPlacingModel) {
                            editor.CancelPlacement();
                        } else if (editor.mode == Surreal::EditorMode::EDIT_MODE) {
                            editor.ToggleMode(playCamera.GetPosition(), playCamera.pitch, playCamera.yaw);
                        } else {
                            running = false;
                        }
                    } else if (sym == SDLK_TAB) {
                        editor.ToggleMode(playCamera.GetPosition(), playCamera.pitch, playCamera.yaw);
                    } else if (sym == SDLK_F9) {
                        hybridRenderer.ToggleBackend();
                        editor.SetStatus("Switched Backend: " + hybridRenderer.GetBackendName(), 3.5f);
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
                            case SDLK_q: playCamera.yaw -= 0.12f; break;
                            case SDLK_e: playCamera.yaw += 0.12f; break;
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
                            case SDLK_r:
                                charPos = Surreal::Vec3(0, 0, 0);
                                charVel = Surreal::Vec3(0, 0, 0);
                                playCamera.distance = 2.8f;
                                playCamera.targetDistance = 2.8f;
                                playCamera.pitch = 0.28f;
                                playCamera.yaw = 0.0f;
                                break;
                            case SDLK_b:
                                if (!isBenchmarkRunning) {
                                    isBenchmarkRunning = true;
                                    benchmarkFrameCount = 0;
                                    benchFpsList.clear();
                                    benchSkinTimes.clear();
                                    benchRenderTimes.clear();
                                    benchResults.isCompleted = false;
                                }
                                break;
                        }
                    }
                    // --- WORLD EDITOR MODE CONTROLS ---
                    else {
                        if (editor.showModelCatalog) {
                            int choice = 0;
                            if (sym >= SDLK_1 && sym <= SDLK_9) choice = sym - SDLK_0;
                            else if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9) choice = sym - SDLK_KP_1 + 1;

                            if (choice >= 1 && choice <= 9) {
                                editor.SelectModelPreset(choice, character.activeDeformedMesh);
                                editor.placementPos = editor.flyCamPos + editor.GetFlyForward() * 3.5f;
                                editor.placementPos.y = std::max(0.0f, editor.placementPos.y);
                            } else if (sym == SDLK_TAB) {
                                editor.CycleModelFolder();
                            } else if (sym == SDLK_F5) {
                                editor.RefreshModelFolder();
                                editor.SetStatus("[✓] Rescanned 3D Model Folder: " + std::to_string(editor.folderModels.size()) + " files found");
                            } else if (sym == SDLK_ESCAPE) {
                                editor.showModelCatalog = false;
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
                    if (editor.mode == Surreal::EditorMode::EDIT_MODE) {
                        if (editor.showModelCatalog && ev.button.button == SDL_BUTTON_LEFT) {
                            int maxItems = std::min(5, static_cast<int>(editor.folderModels.size()));
                            int diagW = 580;
                            int diagH = 260 + maxItems * 22;
                            int diagX = (WIN_WIDTH - diagW) / 2;
                            int diagY = (WIN_HEIGHT - diagH) / 2;

                            if (ev.button.x >= diagX && ev.button.x <= diagX + diagW) {
                                // Click on folder path banner to cycle folder
                                if (ev.button.y >= diagY + 28 && ev.button.y <= diagY + 48) {
                                    editor.CycleModelFolder();
                                } else {
                                    // Click on item line
                                    int row = (ev.button.y - (diagY + 58)) / 22;
                                    if (row >= 0 && row < maxItems) {
                                        // Folder model
                                        editor.SelectModelPreset(row + 1, character.activeDeformedMesh);
                                        editor.placementPos = editor.flyCamPos + editor.GetFlyForward() * 3.5f;
                                        editor.placementPos.y = std::max(0.0f, editor.placementPos.y);
                                    } else if (row >= maxItems + 1 && row <= maxItems + 3) {
                                        // Built-in presets
                                        int presetChoice = maxItems + (row - maxItems);
                                        editor.SelectModelPreset(presetChoice, character.activeDeformedMesh);
                                        editor.placementPos = editor.flyCamPos + editor.GetFlyForward() * 3.5f;
                                        editor.placementPos.y = std::max(0.0f, editor.placementPos.y);
                                    } else if (ev.button.y > diagY + diagH - 30) {
                                        editor.showModelCatalog = false;
                                    }
                                }
                            } else {
                                editor.showModelCatalog = false;
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
                            ev.motion.x, ev.motion.y, WIN_WIDTH, WIN_HEIGHT,
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
            auto tSkinEnd = std::chrono::high_resolution_clock::now();
            lastSkinningMs = std::chrono::duration<float, std::milli>(tSkinEnd - tSkinStart).count();
        }

        // --- 3. Dual-Backend Rendering Execution ---
        auto tRenderStart = std::chrono::high_resolution_clock::now();

        if (hybridRenderer.backend == Surreal::RenderBackend::GPU_OPENGL && hybridRenderer.isGLInitialized) {
            // --- GPU HARDWARE OPENGL 4.6 PIPELINE ---
            hybridRenderer.BeginFrame();
            hybridRenderer.SetGlobalUniforms(
                engine.world.sunLight.direction,
                engine.world.sunLight.color,
                engine.world.ambientLight,
                engine.cameraPos
            );

            Surreal::Mat4 vpMat = engine.GetProjectionMatrix() * engine.GetViewMatrix();

            // Render all scene entities
            for (auto entityID : engine.world.entities) {
                auto itMr = engine.world.meshRenderers.find(entityID);
                auto itTr = engine.world.transforms.find(entityID);
                if (itMr == engine.world.meshRenderers.end() || itTr == engine.world.transforms.end()) continue;

                bool isAvatar = (entityID == avatarEntity);
                hybridRenderer.RenderMesh(itMr->second.mesh, itTr->second.GetWorldMatrix(), vpMat, itMr->second.material, isAvatar);
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

            if (editor.mode == Surreal::EditorMode::PLAY_MODE) {
                Surreal::SurrealHUD::RenderHUD(
                    hudFramebuffer.data(), WIN_WIDTH, WIN_HEIGHT,
                    currentFps, engine.lastFrameTimeMs + lastSkinningMs, lastSkinningMs,
                    engine.renderedMeshlets, engine.culledMeshlets,
                    character.bindVertices.size(), character.activeDeformedMesh->indices.size() / 3,
                    clipName, character.currentTime,
                    playCamera.distance, playCamera.pitch, playCamera.yaw,
                    isBenchmarkRunning, 0,
                    benchResults.isCompleted ? &benchResults : nullptr
                );
            }

            editor.RenderEditorHUD(hudFramebuffer.data(), WIN_WIDTH, WIN_HEIGHT, currentFps, engine.lastFrameTimeMs, hybridRenderer.GetBackendName());
            hybridRenderer.BlitHUDOverlay(hudFramebuffer.data(), WIN_WIDTH, WIN_HEIGHT);
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
            if (editor.mode == Surreal::EditorMode::PLAY_MODE) {
                Surreal::SurrealHUD::RenderHUD(
                    framebuffer.data(), WIN_WIDTH, WIN_HEIGHT,
                    currentFps, engine.lastFrameTimeMs + lastSkinningMs, lastSkinningMs,
                    engine.renderedMeshlets, engine.culledMeshlets,
                    character.bindVertices.size(), character.activeDeformedMesh->indices.size() / 3,
                    clipName, character.currentTime,
                    playCamera.distance, playCamera.pitch, playCamera.yaw,
                    isBenchmarkRunning, 0,
                    benchResults.isCompleted ? &benchResults : nullptr
                );
            }

            editor.RenderEditorHUD(framebuffer.data(), WIN_WIDTH, WIN_HEIGHT, currentFps, engine.lastFrameTimeMs, hybridRenderer.GetBackendName());

            if (!headless && hybridRenderer.isGLInitialized) {
                hybridRenderer.BlitSoftwareFramebuffer(framebuffer.data(), WIN_WIDTH, WIN_HEIGHT);
                hybridRenderer.Present();
            }
        }

        auto tRenderEnd = std::chrono::high_resolution_clock::now();
        float renderMs = std::chrono::duration<float, std::milli>(tRenderEnd - tRenderStart).count();
        engine.lastFrameTimeMs = renderMs;

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
