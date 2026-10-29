#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - 3D GAME STUDIO & LEVEL EDITOR
 * Real-time 3D Scene Inspection, 3D Model Loader, Mouse & Keyboard Placement,
 * Transform Gizmos, Material Tuning & JSON Level Serialization
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_ecs.hpp"
#include "surreal_terrain.hpp"
#include "surreal_camera.hpp"
#include "surreal_hud.hpp"
#include "surreal_model_loader.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iomanip>

namespace Surreal {

enum class EditorMode {
    PLAY_MODE,
    EDIT_MODE
};

struct EditableEntity {
    EntityID id;
    std::string name;
    Vec3 position;
    Vec3 scale;
    ColorRGB color;
    float metallic;
    float roughness;
    bool isStaticTerrain;
    bool isCharacter;

    EditableEntity()
        : id(INVALID_ENTITY), name("Entity"), position(0,0,0), scale(1,1,1),
          color(1,1,1), metallic(0.1f), roughness(0.5f),
          isStaticTerrain(false), isCharacter(false) {}
};

class SurrealEditor {
public:
    EditorMode mode;
    int selectedIndex;
    std::vector<EditableEntity> entities;

    // Free Fly Edit Camera
    Vec3 flyCamPos;
    float flyCamYaw;
    float flyCamPitch;
    float flyCamSpeed;

    // UI & Feedback Notifications
    std::string statusMessage;
    float statusTimer;

    // 3D Model Catalog, Folder Browser & Mouse/Keyboard Placement State
    bool showModelCatalog;
    bool isPlacingModel;
    ModelPresetType activePreset;
    std::shared_ptr<SurrealMesh> placementMesh;
    Vec3 placementPos;
    float placementYaw;
    Vec3 placementScale;
    ColorRGB placementColor;
    float placementMetallic;
    float placementRoughness;
    std::string placementName;

    // 3D Model Folder Browser
    std::vector<std::string> modelFolders = {
        "assets/models",
        "/root/coding/MeinOS/surreal_engine/assets/models",
        "assets/custom_models",
        "/root/Downloads"
    };
    int activeFolderIndex = 0;
    std::vector<DiscoveredModelFile> folderModels;

    // Color Preset Palette
    const std::vector<ColorRGB> colorPalette = {
        ColorRGB(0.00f, 0.95f, 0.85f), // Cyan
        ColorRGB(1.00f, 0.15f, 0.65f), // Hot Magenta
        ColorRGB(1.00f, 0.85f, 0.20f), // Radiant Gold
        ColorRGB(0.00f, 1.00f, 0.50f), // Neon Lime
        ColorRGB(0.95f, 0.20f, 0.20f), // Crimson Red
        ColorRGB(0.30f, 0.65f, 1.00f), // Apex Azure
        ColorRGB(0.70f, 0.20f, 0.95f), // Cyber Violet
        ColorRGB(0.95f, 0.95f, 0.95f), // Pure Silver/White
        ColorRGB(0.20f, 0.22f, 0.28f)  // Dark Obsidian
    };
    size_t currentPaletteIdx;

    SurrealEditor()
        : mode(EditorMode::PLAY_MODE),
          selectedIndex(2),
          flyCamPos(0.0f, 3.5f, 6.0f),
          flyCamYaw(0.0f),
          flyCamPitch(0.35f),
          flyCamSpeed(6.0f),
          statusMessage(""),
          statusTimer(0.0f),
          showModelCatalog(false),
          isPlacingModel(false),
          activePreset(ModelPresetType::MONOLITH),
          placementMesh(nullptr),
          placementPos(0, 0, 0),
          placementYaw(0.0f),
          placementScale(1, 1, 1),
          placementColor(0.0f, 0.95f, 0.85f),
          placementMetallic(0.4f),
          placementRoughness(0.35f),
          placementName("New_Model"),
          currentPaletteIdx(0) {}

    void SetStatus(const std::string& msg, float duration = 3.0f) {
        statusMessage = msg;
        statusTimer = duration;
    }

    void Update(float dt) {
        if (statusTimer > 0.0f) {
            statusTimer -= dt;
            if (statusTimer <= 0.0f) {
                statusMessage = "";
            }
        }
    }

    void ToggleMode(const Vec3& currentCamPos = Vec3(0, 2.5f, 5.0f), float currentPitch = 0.25f, float currentYaw = 0.0f) {
        if (mode == EditorMode::PLAY_MODE) {
            mode = EditorMode::EDIT_MODE;
            flyCamPos = currentCamPos;
            flyCamPitch = -currentPitch;
            flyCamYaw = currentYaw;
            showModelCatalog = false;
            isPlacingModel = false;
            SetStatus("[!] Switched to 3D WORLD EDITOR MODE [TAB]");
        } else {
            mode = EditorMode::PLAY_MODE;
            showModelCatalog = false;
            isPlacingModel = false;
            SetStatus("[✓] Switched to 3D PLAY MODE [TAB]");
        }
    }

    void SelectNext() {
        if (entities.empty()) return;
        selectedIndex = (selectedIndex + 1) % static_cast<int>(entities.size());
        SetStatus("Selected: " + entities[selectedIndex].name);
    }

    void SelectPrev() {
        if (entities.empty()) return;
        selectedIndex = (selectedIndex - 1 + static_cast<int>(entities.size())) % static_cast<int>(entities.size());
        SetStatus("Selected: " + entities[selectedIndex].name);
    }

    void SelectIndex(int idx) {
        if (idx >= 0 && idx < static_cast<int>(entities.size())) {
            selectedIndex = idx;
            SetStatus("Selected: " + entities[selectedIndex].name);
        }
    }

    EditableEntity* GetSelectedEntity() {
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(entities.size())) {
            return &entities[selectedIndex];
        }
        return nullptr;
    }

    // --- 3D MODEL LOADER, FOLDER IMPORTER & PLACEMENT LOGIC ---

    void RefreshModelFolder() {
        folderModels.clear();
        std::string currentPath = modelFolders[activeFolderIndex];
        folderModels = SurrealModelLoader::ScanFolder(currentPath);

        // Fallback search if current path yields no models
        if (folderModels.empty()) {
            for (size_t i = 0; i < modelFolders.size(); ++i) {
                auto res = SurrealModelLoader::ScanFolder(modelFolders[i]);
                if (!res.empty()) {
                    activeFolderIndex = static_cast<int>(i);
                    folderModels = res;
                    break;
                }
            }
        }
    }

    void CycleModelFolder() {
        activeFolderIndex = (activeFolderIndex + 1) % modelFolders.size();
        folderModels = SurrealModelLoader::ScanFolder(modelFolders[activeFolderIndex]);
        SetStatus("[*] Switched 3D Folder: " + modelFolders[activeFolderIndex] + " (" + std::to_string(folderModels.size()) + " models found)");
    }

    void ToggleModelCatalog() {
        showModelCatalog = !showModelCatalog;
        if (showModelCatalog) {
            RefreshModelFolder();
            SetStatus("[*] 3D Model Importer Open: Choose model from folder or preset", 4.0f);
        }
    }

    void SelectModelPreset(int choice, const std::shared_ptr<SurrealMesh>& avatarMesh = nullptr) {
        placementScale = Vec3(1.0f, 1.0f, 1.0f);
        placementYaw = 0.0f;
        placementColor = colorPalette[currentPaletteIdx % colorPalette.size()];
        placementMetallic = 0.45f;
        placementRoughness = 0.35f;

        // 1. If choice is within discovered folder files:
        if (choice >= 1 && choice <= static_cast<int>(folderModels.size())) {
            const auto& file = folderModels[choice - 1];
            bool loaded = false;
            if (file.extension == ".obj") {
                loaded = SurrealModelLoader::LoadCustomFile(file.fullPath, placementMesh, placementColor);
            } else if (file.extension == ".gltf" || file.extension == ".glb") {
                SkeletalMeshInstance inst;
                loaded = SurrealSkeletalLoader::LoadSkeletalModel(file.fullPath, inst);
                if (loaded) placementMesh = inst.activeDeformedMesh;
            }

            if (!loaded || !placementMesh) {
                SetStatus("[-] Failed to load 3D model file: " + file.filename, 4.0f);
                return;
            }

            // Clean name without extension
            std::string baseName = file.filename;
            size_t dot = baseName.rfind('.');
            if (dot != std::string::npos) baseName = baseName.substr(0, dot);
            placementName = baseName + "_" + std::to_string(entities.size() + 1);

            // Normalize scale
            placementMesh->CalculateBounds();
            Vec3 ext = placementMesh->worldBounds.Extents() * 2.0f;
            float maxDim = std::max({ext.x, ext.y, ext.z});
            float scaleFactor = 1.0f;
            if (maxDim > 0.001f) {
                if (maxDim > 50.0f) scaleFactor = 2.0f / maxDim;
                else if (maxDim < 0.2f) scaleFactor = 1.5f / maxDim;
                else scaleFactor = 1.8f / maxDim;
            }
            placementScale = Vec3(scaleFactor, scaleFactor, scaleFactor);
            activePreset = ModelPresetType::CUSTOM_FILE;
        }
        // 2. Otherwise it's a built-in procedural preset:
        else {
            int presetIdx = choice - static_cast<int>(folderModels.size());
            switch (presetIdx) {
                case 1:
                    placementName = "ParkourRamp_" + std::to_string(entities.size() + 1);
                    placementScale = Vec3(2.2f, 1.5f, 3.8f);
                    placementMesh = SurrealModelLoader::CreateRamp(placementScale, placementColor);
                    activePreset = ModelPresetType::RAMP;
                    break;
                case 2:
                    placementName = "CyberMonolith_" + std::to_string(entities.size() + 1);
                    placementScale = Vec3(1.4f, 3.2f, 1.4f);
                    placementMesh = SurrealModelLoader::CreateMonolith(placementScale, placementColor);
                    activePreset = ModelPresetType::MONOLITH;
                    break;
                case 3:
                    placementName = "QuantumCore_" + std::to_string(entities.size() + 1);
                    placementScale = Vec3(1.6f, 1.6f, 1.6f);
                    placementMesh = SurrealModelLoader::CreateOrb(0.85f, 18, 24, placementColor);
                    activePreset = ModelPresetType::ORB;
                    break;
                default:
                case 4:
                    placementName = "AvatarClone_" + std::to_string(entities.size() + 1);
                    placementScale = Vec3(0.01f, 0.01f, 0.01f);
                    placementMesh = avatarMesh ? avatarMesh : SurrealModelLoader::CreateMonolith(Vec3(1, 2, 1), placementColor);
                    activePreset = ModelPresetType::AVATAR_CLONE;
                    break;
            }
        }

        showModelCatalog = false;
        isPlacingModel = true;
        SetStatus("[+] PLACING: " + placementName + " | Move Mouse, Left-Click to drop, [R] Rotate, [ESC] Cancel", 5.0f);
    }

    // Screen to Ground Raycasting for Mouse Placement
    static Vec3 ScreenToGround(
        int mouseX, int mouseY, int screenW, int screenH,
        const Vec3& camPos, const Vec3& camTarget, const Vec3& camUp,
        float fovRad, const std::vector<ObstacleBox>& obstacles)
    {
        float aspect = static_cast<float>(screenW) / static_cast<float>(screenH);
        float nx = (2.0f * mouseX / screenW) - 1.0f;
        float ny = 1.0f - (2.0f * mouseY / screenH);
        float tanHalf = tanf(fovRad * 0.5f);

        float vx = nx * aspect * tanHalf;
        float vy = ny * tanHalf;

        Vec3 f = (camTarget - camPos).Normalized();
        Vec3 r = f.Cross(camUp).Normalized();
        Vec3 u = r.Cross(f);

        Vec3 rayDir = (r * vx + u * vy + f).Normalized();

        // 1. Intersect platform tops
        float bestT = 100000.0f;
        Vec3 bestHit(0, 0, 0);
        bool hitFound = false;

        for (const auto& obs : obstacles) {
            float topY = obs.topY;
            if (fabs(rayDir.y) > 0.001f) {
                float t = (topY - camPos.y) / rayDir.y;
                if (t > 0.0f && t < bestT) {
                    Vec3 hit = camPos + (rayDir * t);
                    if (hit.x >= obs.aabb.min.x && hit.x <= obs.aabb.max.x &&
                        hit.z >= obs.aabb.min.z && hit.z <= obs.aabb.max.z)
                    {
                        bestT = t;
                        bestHit = hit;
                        hitFound = true;
                    }
                }
            }
        }

        // 2. Intersect ground plane Y = 0
        if (fabs(rayDir.y) > 0.001f) {
            float t = (0.0f - camPos.y) / rayDir.y;
            if (t > 0.0f && (!hitFound || t < bestT)) {
                bestT = t;
                bestHit = camPos + (rayDir * t);
                hitFound = true;
            }
        }

        if (!hitFound) {
            bestHit = camPos + f * 5.0f;
            bestHit.y = 0.0f;
        }

        return bestHit;
    }

    void ConfirmPlacement(SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        if (!isPlacingModel || !placementMesh) return;

        EntityID newEntity = world.CreateEntity(placementName);
        TransformComponent& tr = world.transforms[newEntity];
        tr.position = placementPos;
        tr.scale = placementScale;
        tr.rotation = Quat::FromAxisAngle(Vec3(0, 1.0f, 0), placementYaw);

        MeshRendererComponent mr;
        mr.mesh = placementMesh;
        mr.material.albedo = placementColor;
        mr.material.metallic = placementMetallic;
        mr.material.roughness = placementRoughness;
        world.meshRenderers[newEntity] = mr;

        EditableEntity ed;
        ed.id = newEntity;
        ed.name = placementName;
        ed.position = placementPos;
        ed.scale = placementScale;
        ed.color = placementColor;
        ed.metallic = placementMetallic;
        ed.roughness = placementRoughness;
        ed.isStaticTerrain = false;
        ed.isCharacter = (activePreset == ModelPresetType::AVATAR_CLONE);

        entities.push_back(ed);
        selectedIndex = static_cast<int>(entities.size() - 1);

        SyncObstacles(obstacles);
        isPlacingModel = false;
        SetStatus("[✓] Successfully placed model: " + placementName, 4.0f);
    }

    void CancelPlacement() {
        if (isPlacingModel) {
            isPlacingModel = false;
            SetStatus("[!] Model placement cancelled.");
        }
        if (showModelCatalog) {
            showModelCatalog = false;
        }
    }

    // Move selected entity along world axes
    void MoveSelected(const Vec3& delta, SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        EditableEntity* ent = GetSelectedEntity();
        if (!ent || ent->isStaticTerrain) return;

        ent->position = ent->position + delta;
        if (world.transforms.find(ent->id) != world.transforms.end()) {
            world.transforms[ent->id].position = ent->position;
        }

        SyncObstacles(obstacles);
        SetStatus("Moved " + ent->name + " -> (" + FormatVec(ent->position) + ")");
    }

    // Scale selected entity
    void ScaleSelected(float factor, SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        EditableEntity* ent = GetSelectedEntity();
        if (!ent || ent->isStaticTerrain || ent->isCharacter) return;

        ent->scale = ent->scale * factor;
        ent->scale.x = std::max(0.2f, ent->scale.x);
        ent->scale.y = std::max(0.1f, ent->scale.y);
        ent->scale.z = std::max(0.2f, ent->scale.z);

        if (world.meshRenderers.find(ent->id) != world.meshRenderers.end()) {
            world.meshRenderers[ent->id].mesh = SurrealTerrainBuilder::CreateBox(ent->scale, ent->color);
        }

        SyncObstacles(obstacles);
        SetStatus("Scaled " + ent->name + " -> (" + FormatVec(ent->scale) + ")");
    }

    // Cycle material color
    void CycleSelectedColor(SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        currentPaletteIdx = (currentPaletteIdx + 1) % colorPalette.size();
        ColorRGB newColor = colorPalette[currentPaletteIdx];

        if (isPlacingModel) {
            placementColor = newColor;
            SetStatus("Placement Color Updated");
            return;
        }

        EditableEntity* ent = GetSelectedEntity();
        if (!ent || ent->isStaticTerrain || ent->isCharacter) return;

        ent->color = newColor;
        if (world.meshRenderers.find(ent->id) != world.meshRenderers.end()) {
            world.meshRenderers[ent->id].material.albedo = ent->color;
            world.meshRenderers[ent->id].mesh = SurrealTerrainBuilder::CreateBox(ent->scale, ent->color);
        }

        SyncObstacles(obstacles);
        SetStatus("Color Tint: " + ent->name + " Updated");
    }

    // Cycle metallic & roughness
    void CycleSelectedPBR(SurrealWorld& world) {
        if (isPlacingModel) {
            placementMetallic = (placementMetallic < 0.5f) ? 0.9f : 0.2f;
            placementRoughness = (placementMetallic > 0.5f) ? 0.25f : 0.55f;
            SetStatus("Placement Metallic: " + std::to_string(placementMetallic).substr(0,4));
            return;
        }

        EditableEntity* ent = GetSelectedEntity();
        if (!ent || ent->isStaticTerrain) return;

        if (ent->metallic < 0.3f) {
            ent->metallic = 0.50f;
            ent->roughness = 0.35f;
        } else if (ent->metallic < 0.7f) {
            ent->metallic = 0.90f;
            ent->roughness = 0.20f;
        } else {
            ent->metallic = 0.10f;
            ent->roughness = 0.60f;
        }

        if (world.meshRenderers.find(ent->id) != world.meshRenderers.end()) {
            world.meshRenderers[ent->id].material.metallic = ent->metallic;
            world.meshRenderers[ent->id].material.roughness = ent->roughness;
        }
        SetStatus("PBR Metallic: " + std::to_string(ent->metallic).substr(0,4));
    }

    // Spawn new Platform / Box
    void SpawnPlatform(SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        std::string newName = "Platform_" + std::to_string(entities.size() + 1);
        Vec3 spawnPos = flyCamPos + GetFlyForward() * 3.5f;
        spawnPos.y = std::max(0.35f, spawnPos.y);

        ColorRGB newColor = colorPalette[(entities.size()) % colorPalette.size()];
        Vec3 newScale(1.8f, 0.6f, 1.8f);

        EntityID platEntity = world.CreateEntity(newName);
        TransformComponent& pt = world.transforms[platEntity];
        pt.position = spawnPos;

        MeshRendererComponent platRenderer;
        platRenderer.mesh = SurrealTerrainBuilder::CreateBox(newScale, newColor);
        platRenderer.material.albedo = newColor;
        platRenderer.material.metallic = 0.35f;
        platRenderer.material.roughness = 0.40f;
        world.meshRenderers[platEntity] = platRenderer;

        EditableEntity ed;
        ed.id = platEntity;
        ed.name = newName;
        ed.position = spawnPos;
        ed.scale = newScale;
        ed.color = newColor;
        ed.metallic = 0.35f;
        ed.roughness = 0.40f;
        ed.isStaticTerrain = false;
        ed.isCharacter = false;

        entities.push_back(ed);
        selectedIndex = static_cast<int>(entities.size() - 1);

        SyncObstacles(obstacles);
        SetStatus("[+] Spawned New Platform: " + newName);
    }

    // Duplicate Selected Entity
    void DuplicateSelected(SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        EditableEntity* src = GetSelectedEntity();
        if (!src || src->isStaticTerrain || src->isCharacter) return;

        std::string dupName = src->name + "_Copy";
        Vec3 dupPos = src->position + Vec3(1.2f, 0.3f, 0.0f);

        EntityID platEntity = world.CreateEntity(dupName);
        TransformComponent& pt = world.transforms[platEntity];
        pt.position = dupPos;

        MeshRendererComponent platRenderer;
        platRenderer.mesh = SurrealTerrainBuilder::CreateBox(src->scale, src->color);
        platRenderer.material.albedo = src->color;
        platRenderer.material.metallic = src->metallic;
        platRenderer.material.roughness = src->roughness;
        world.meshRenderers[platEntity] = platRenderer;

        EditableEntity ed;
        ed.id = platEntity;
        ed.name = dupName;
        ed.position = dupPos;
        ed.scale = src->scale;
        ed.color = src->color;
        ed.metallic = src->metallic;
        ed.roughness = src->roughness;
        ed.isStaticTerrain = false;
        ed.isCharacter = false;

        entities.push_back(ed);
        selectedIndex = static_cast<int>(entities.size() - 1);

        SyncObstacles(obstacles);
        SetStatus("[+] Cloned: " + dupName);
    }

    // Delete Selected Entity
    void DeleteSelected(SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        EditableEntity* ent = GetSelectedEntity();
        if (!ent || ent->isStaticTerrain || ent->isCharacter) {
            SetStatus("[!] Cannot delete core scene entity!");
            return;
        }

        std::string delName = ent->name;
        world.DestroyEntity(ent->id);
        entities.erase(entities.begin() + selectedIndex);
        if (selectedIndex >= static_cast<int>(entities.size())) {
            selectedIndex = static_cast<int>(entities.size() - 1);
        }

        SyncObstacles(obstacles);
        SetStatus("[-] Deleted Entity: " + delName);
    }

    // Save Level to JSON
    bool SaveLevel(const std::string& filepath) {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            SetStatus("[-] Error saving level to " + filepath);
            return false;
        }

        file << "{\n";
        file << "  \"engine\": \"AION Surreal 3D Engine\",\n";
        file << "  \"version\": 1.0,\n";
        file << "  \"entity_count\": " << entities.size() << ",\n";
        file << "  \"entities\": [\n";

        for (size_t i = 0; i < entities.size(); ++i) {
            const auto& e = entities[i];
            file << "    {\n";
            file << "      \"id\": " << e.id << ",\n";
            file << "      \"name\": \"" << e.name << "\",\n";
            file << "      \"type\": \"" << (e.isStaticTerrain ? "TERRAIN" : (e.isCharacter ? "AVATAR" : "PLATFORM")) << "\",\n";
            file << "      \"position\": [" << e.position.x << ", " << e.position.y << ", " << e.position.z << "],\n";
            file << "      \"scale\": [" << e.scale.x << ", " << e.scale.y << ", " << e.scale.z << "],\n";
            file << "      \"color\": [" << e.color.r << ", " << e.color.g << ", " << e.color.b << "],\n";
            file << "      \"metallic\": " << e.metallic << ",\n";
            file << "      \"roughness\": " << e.roughness << "\n";
            file << "    }" << (i + 1 < entities.size() ? "," : "") << "\n";
        }

        file << "  ]\n";
        file << "}\n";
        file.close();

        SetStatus("[✓] LEVEL SAVED -> " + filepath, 4.0f);
        return true;
    }

    // Load Level from JSON
    bool LoadLevel(const std::string& filepath, SurrealWorld& world, std::vector<ObstacleBox>& obstacles) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            SetStatus("[-] Cannot open file: " + filepath);
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        std::vector<EditableEntity> kept;
        for (const auto& e : entities) {
            if (e.isStaticTerrain || e.isCharacter) {
                kept.push_back(e);
            } else {
                world.DestroyEntity(e.id);
            }
        }
        entities = kept;

        size_t pos = 0;
        while ((pos = content.find("\"name\":", pos)) != std::string::npos) {
            size_t nameStart = content.find("\"", pos + 7) + 1;
            size_t nameEnd = content.find("\"", nameStart);
            std::string name = content.substr(nameStart, nameEnd - nameStart);

            if (name == "CyberGridFloor" || name == "SurrealAvatarPrime") {
                pos = nameEnd;
                continue;
            }

            Vec3 p(0, 0.5f, 0);
            size_t pStart = content.find("\"position\":", nameEnd);
            if (pStart != std::string::npos) {
                size_t b0 = content.find("[", pStart) + 1;
                size_t b1 = content.find("]", b0);
                std::stringstream ss(content.substr(b0, b1 - b0));
                char comma;
                ss >> p.x >> comma >> p.y >> comma >> p.z;
            }

            Vec3 s(1.0f, 1.0f, 1.0f);
            size_t sStart = content.find("\"scale\":", nameEnd);
            if (sStart != std::string::npos) {
                size_t b0 = content.find("[", sStart) + 1;
                size_t b1 = content.find("]", b0);
                std::stringstream ss(content.substr(b0, b1 - b0));
                char comma;
                ss >> s.x >> comma >> s.y >> comma >> s.z;
            }

            ColorRGB col(1.0f, 1.0f, 1.0f);
            size_t cStart = content.find("\"color\":", nameEnd);
            if (cStart != std::string::npos) {
                size_t b0 = content.find("[", cStart) + 1;
                size_t b1 = content.find("]", b0);
                std::stringstream ss(content.substr(b0, b1 - b0));
                char comma;
                ss >> col.r >> comma >> col.g >> comma >> col.b;
            }

            float m = 0.35f, r = 0.40f;
            size_t mStart = content.find("\"metallic\":", nameEnd);
            if (mStart != std::string::npos) {
                size_t valStart = mStart + 11;
                m = std::stof(content.substr(valStart, 6));
            }
            size_t rStart = content.find("\"roughness\":", nameEnd);
            if (rStart != std::string::npos) {
                size_t valStart = rStart + 12;
                r = std::stof(content.substr(valStart, 6));
            }

            EntityID platEntity = world.CreateEntity(name);
            TransformComponent& pt = world.transforms[platEntity];
            pt.position = p;

            MeshRendererComponent platRenderer;
            platRenderer.mesh = SurrealTerrainBuilder::CreateBox(s, col);
            platRenderer.material.albedo = col;
            platRenderer.material.metallic = m;
            platRenderer.material.roughness = r;
            world.meshRenderers[platEntity] = platRenderer;

            EditableEntity ed;
            ed.id = platEntity;
            ed.name = name;
            ed.position = p;
            ed.scale = s;
            ed.color = col;
            ed.metallic = m;
            ed.roughness = r;
            ed.isStaticTerrain = false;
            ed.isCharacter = false;

            entities.push_back(ed);
            pos = nameEnd;
        }

        selectedIndex = std::min(selectedIndex, static_cast<int>(entities.size() - 1));
        SyncObstacles(obstacles);
        SetStatus("[✓] LEVEL LOADED -> " + filepath, 4.0f);
        return true;
    }

    void SyncObstacles(std::vector<ObstacleBox>& obstacles) {
        obstacles.clear();
        for (const auto& e : entities) {
            if (e.isStaticTerrain || e.isCharacter) continue;
            Vec3 halfSize = e.scale * 0.5f;
            Vec3 center = e.position;
            // For models whose base sits on the ground / placement plane, the center is offset up by halfSize.y
            if (e.name.find("Platform") == std::string::npos) {
                center.y = e.position.y + halfSize.y;
            }
            obstacles.emplace_back(center, halfSize, e.name, e.color);
        }
    }

    Vec3 GetFlyForward() const {
        return Vec3(
            -sinf(flyCamYaw) * cosf(flyCamPitch),
            sinf(flyCamPitch),
            -cosf(flyCamYaw) * cosf(flyCamPitch)
        ).Normalized();
    }

    Vec3 GetFlyRight() const {
        return GetFlyForward().Cross(Vec3(0, 1.0f, 0)).Normalized();
    }

    // Render 3D World Editor HUD & Outliner & Inspector & Spawner Modals
    void RenderEditorHUD(uint32_t* fb, int screenW, int screenH, float currentFps, float frameTimeMs, const std::string& backendName) {
        // Top Status Header
        std::string modeTitle = (mode == EditorMode::EDIT_MODE) ? "SURREAL STUDIO // [EDIT MODE]" : "SURREAL STUDIO // [PLAY MODE]";
        uint32_t modeColor = (mode == EditorMode::EDIT_MODE) ? 0xFFFF007F : 0xFF00FFCC;

        SurrealHUD::DrawPanel(fb, screenW, screenH, 12, 12, screenW - 24, 38, 0x050812, 0.90f);
        SurrealHUD::DrawString(fb, screenW, screenH, 24, 22, modeTitle, modeColor, 1);

        char metaBuf[256];
        std::snprintf(metaBuf, sizeof(metaBuf), "FPS: %.1f | %s | [TAB] Switch",
                      currentFps, backendName.c_str());
        SurrealHUD::DrawString(fb, screenW, screenH, screenW - 480, 22, metaBuf, 0xFF00FF88, 1);

        // In EDIT_MODE: Render Scene Outliner & Inspector
        if (mode == EditorMode::EDIT_MODE) {
            int panelW = 275;
            int panelX = screenW - panelW - 12;

            // 1. SCENE OUTLINER PANEL
            int outlinerH = 260;
            SurrealHUD::DrawPanel(fb, screenW, screenH, panelX, 56, panelW, outlinerH, 0x070B18, 0.92f);
            SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, 68, "=== SCENE OUTLINER ===", 0xFF00FFCC, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, 80, "------------------------", 0xFF336688, 1);

            int startY = 94;
            for (size_t i = 0; i < entities.size() && i < 11; ++i) {
                bool isSel = (static_cast<int>(i) == selectedIndex);
                uint32_t textColor = isSel ? 0xFF00FF88 : 0xFFCCCCCC;
                std::string prefix = isSel ? "> [" : "  [";
                std::string suffix = isSel ? "] <" : "]";

                char itemBuf[64];
                std::snprintf(itemBuf, sizeof(itemBuf), "%s%d%s %s", prefix.c_str(), (int)i, suffix.c_str(), entities[i].name.c_str());
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, startY + (int)i * 14, itemBuf, textColor, 1);
            }

            SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, 56 + outlinerH - 24,
                                   "[N/P] Next/Prev | [1-9] Select", 0xFF88AABB, 1);

            // 2. TRANSFORM & PBR INSPECTOR PANEL
            int inspectorY = 56 + outlinerH + 8;
            int inspectorH = screenH - inspectorY - 12;
            SurrealHUD::DrawPanel(fb, screenW, screenH, panelX, inspectorY, panelW, inspectorH, 0x070B18, 0.92f);

            EditableEntity* sel = GetSelectedEntity();
            if (sel) {
                char titleBuf[64];
                std::snprintf(titleBuf, sizeof(titleBuf), "INSPECTOR: %s", sel->name.c_str());
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 12, titleBuf, 0xFFFFCC00, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 24, "------------------------", 0xFF336688, 1);

                char transBuf[128];
                std::snprintf(transBuf, sizeof(transBuf), "Pos:  X:%.2f Y:%.2f Z:%.2f", sel->position.x, sel->position.y, sel->position.z);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 38, transBuf, 0xFFE0E0E0, 1);

                std::snprintf(transBuf, sizeof(transBuf), "Size: W:%.2f H:%.2f D:%.2f", sel->scale.x, sel->scale.y, sel->scale.z);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 54, transBuf, 0xFFE0E0E0, 1);

                std::snprintf(transBuf, sizeof(transBuf), "PBR:  Metal:%.2f Rough:%.2f", sel->metallic, sel->roughness);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 70, transBuf, 0xFF00E5FF, 1);

                // Swatch preview box
                uint32_t previewCol = (0xFF << 24) |
                                      (static_cast<int>(sel->color.r * 255.0f) << 16) |
                                      (static_cast<int>(sel->color.g * 255.0f) << 8) |
                                      static_cast<int>(sel->color.b * 255.0f);
                for (int py = inspectorY + 86; py < inspectorY + 98; ++py) {
                    for (int px = panelX + 14; px < panelX + 14 + 40; ++px) {
                        fb[py * screenW + px] = previewCol;
                    }
                }
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 62, inspectorY + 88, "Albedo Color [C]", 0xFFE0E0E0, 1);

                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 112, "[L] Load & Place Model", 0xFF00FF88, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 126, "[I/K/J/L/U/O] Move Keys", 0xFF00FFCC, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 142, "[+/-] Scale Entity [H]", 0xFFFFAA00, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 156, "[K] Spawn Box | [SH+D] Clone", 0xFFFF007F, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 170, "[DEL] Delete Entity", 0xFFFF3366, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 188, "[F6] Save | [F7] Load JSON", 0xFF00FF88, 1);
                SurrealHUD::DrawString(fb, screenW, screenH, panelX + 14, inspectorY + 202, "[F9] Toggle CPU/GPU Hybrid", 0xFF00E5FF, 1);
            }
        }

        // 3. 3D Model Spawner & Asset Browser Modal Dialog
        if (showModelCatalog) {
            int diagW = 580;
            int maxItems = std::min(5, static_cast<int>(folderModels.size()));
            int diagH = 260 + maxItems * 22;
            int diagX = (screenW - diagW) / 2;
            int diagY = (screenH - diagH) / 2;

            SurrealHUD::DrawPanel(fb, screenW, screenH, diagX, diagY, diagW, diagH, 0x050A18, 0.96f);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, diagY + 16, "=== 3D MODEL FOLDER IMPORTER & BROWSER ===", 0xFF00FFCC, 1);

            char folderBuf[128];
            std::string dispFolder = modelFolders[activeFolderIndex % modelFolders.size()];
            std::snprintf(folderBuf, sizeof(folderBuf), "Folder: [%s] (%zu files found)",
                          dispFolder.c_str(), folderModels.size());
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, diagY + 34, folderBuf, 0xFF88AABB, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, diagY + 46, "-------------------------------------------------------------", 0xFF335577, 1);

            int yPos = diagY + 58;
            int itemNum = 1;

            // List Discovered Files from Folder
            for (size_t i = 0; i < folderModels.size() && itemNum <= 5; ++i, ++itemNum) {
                const auto& item = folderModels[i];
                char lineBuf[128];
                float kb = item.sizeBytes / 1024.0f;
                std::snprintf(lineBuf, sizeof(lineBuf), "[%d] %-22s  %.1f KB  [%s]",
                              itemNum, item.filename.c_str(), kb, item.extension.c_str());
                SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos, lineBuf, 0xFF00E5FF, 1);
                yPos += 22;
            }

            // Built-in Presets
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos + 4, "--- Engine Built-in Presets ---", 0xFF667788, 1);
            yPos += 22;

            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos,
                                   "[" + std::to_string(itemNum) + "] Parkour Ramp (Walkable Wedge Slope)", 0xFFFF8800, 1);
            yPos += 22; itemNum++;

            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos,
                                   "[" + std::to_string(itemNum) + "] Cyber Monolith (Sci-Fi Beveled Obelisk)", 0xFFFFCC00, 1);
            yPos += 22; itemNum++;

            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos,
                                   "[" + std::to_string(itemNum) + "] Mia Avatar Clone (glTF Rigged Character)", 0xFFFF007F, 1);
            yPos += 26;

            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos,
                                   "Click or Press [1]-[8] | [TAB] Switch Folder | [F5] Rescan | [ESC] Cancel", 0xFF00FF88, 1);
        }

        // 4. Mouse / Keyboard Placement HUD Banner
        if (isPlacingModel) {
            int retW = 620;
            int retX = (screenW - retW) / 2;
            int retY = screenH - 76;
            SurrealHUD::DrawPanel(fb, screenW, screenH, retX, retY, retW, 60, 0x050D1E, 0.96f);

            char placeBuf[128];
            std::snprintf(placeBuf, sizeof(placeBuf), "PLACING: %s at (%.1f, %.2f, %.1f) | Yaw: %.0f deg",
                          placementName.c_str(), placementPos.x, placementPos.y, placementPos.z, placementYaw * 180.0f / M_PI);
            SurrealHUD::DrawString(fb, screenW, screenH, retX + 16, retY + 12, placeBuf, 0xFFFF007F, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, retX + 16, retY + 34,
                                   "[L-CLICK / ENTER] Drop | [R] Rotate | [+/-] Scale | [C] Color | [ESC] Cancel", 0xFF00FFCC, 1);
        }

        // 5. Notification Toast
        if (!statusMessage.empty() && !isPlacingModel && !showModelCatalog) {
            int toastW = 540;
            int toastX = (screenW - toastW) / 2;
            int toastY = screenH - 52;
            SurrealHUD::DrawPanel(fb, screenW, screenH, toastX, toastY, toastW, 36, 0x050D1E, 0.95f);
            SurrealHUD::DrawString(fb, screenW, screenH, toastX + 16, toastY + 12, statusMessage, 0xFF00FFCC, 1);
        }
    }

private:
    static std::string FormatVec(const Vec3& v) {
        char b[64];
        std::snprintf(b, sizeof(b), "%.1f, %.1f, %.1f", v.x, v.y, v.z);
        return std::string(b);
    }
};

} // namespace Surreal
