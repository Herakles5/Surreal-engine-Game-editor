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
#include "surreal_image3d.hpp"
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
        "/root/coding/Surreal-engine-Game-editor/assets/models",
        "/root/coding/MeinOS/surreal_engine/assets/models",
        "assets/models",
        "/opt/meinos",
        "assets/custom_models",
        "/root/Downloads"
    };
    int activeFolderIndex = 0;
    std::vector<DiscoveredModelFile> folderModels;

    enum class CatalogCategory {
        DANCERS = 0,
        AVATARS = 1,
        WORLDS_PROPS = 2,
        ALL = 3,
        PHOTO3D = 4   // pictures in <models>/photo3d/ -> coloured 3D model (Photo-to-3D)
    };
    CatalogCategory activeCategory = CatalogCategory::DANCERS;
    std::vector<DiscoveredModelFile> activeCatalogList;

    // Placed skeletal animated entities (NPC dancers in the world)
    std::shared_ptr<SkeletalMeshInstance> placementSkeletal;
    std::unordered_map<EntityID, std::shared_ptr<SkeletalMeshInstance>> animatedEntities;

    int catalogPage = 0;
    static constexpr int ITEMS_PER_PAGE = 8;

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

    static bool IsDanceModel(const std::string& name) {
        std::string lower = name;
        for (char& c : lower) c = tolower(c);
        return (lower.find("dance") != std::string::npos ||
                lower.find("dancing") != std::string::npos ||
                lower.find("gangnam") != std::string::npos ||
                lower.find("samba") != std::string::npos ||
                lower.find("hip_hop") != std::string::npos ||
                lower.find("chicken") != std::string::npos ||
                lower.find("spooky") != std::string::npos ||
                lower.find("miku") != std::string::npos ||
                lower.find("ivonne") != std::string::npos ||
                lower.find("moon") != std::string::npos ||
                lower.find("monk") != std::string::npos ||
                lower.find("dragon") != std::string::npos ||
                lower.find("swimsuit") != std::string::npos ||
                lower.find("solar") != std::string::npos);
    }

    static bool IsAvatarModel(const std::string& name) {
        std::string lower = name;
        for (char& c : lower) c = tolower(c);
        return (lower.find("aion") != std::string::npos ||
                lower.find("avatar") != std::string::npos ||
                lower.find("model_") != std::string::npos ||
                lower.find("mia") != std::string::npos);
    }

    static bool IsWorldOrPropModel(const std::string& name) {
        std::string lower = name;
        for (char& c : lower) c = tolower(c);
        return (lower.find("city") != std::string::npos ||
                lower.find("seoul") != std::string::npos ||
                lower.find("pylon") != std::string::npos ||
                lower.find("drone") != std::string::npos ||
                lower.find("turret") != std::string::npos ||
                lower.find("bike") != std::string::npos);
    }

    static std::string GetCleanName(const std::string& filename) {
        std::string lower = filename;
        for (char& c : lower) c = tolower(c);
        if (lower.find("wednesday") != std::string::npos) return "Wednesday Gangnam Style";
        if (lower.find("miku") != std::string::npos) return "Hatsune Miku Pop Dance";
        if (lower.find("stormtrooper") != std::string::npos) return "Dancing Stormtrooper";
        if (lower.find("yua_han") != std::string::npos) return "Yua Han Samba Dance";
        if (lower.find("skeleton_chicken") != std::string::npos) return "Skeleton Chicken Dance";
        if (lower.find("spooky_skeleton") != std::string::npos) return "Spooky Skeleton Dance";
        if (lower.find("hip_hop") != std::string::npos) return "Hip Hop Breakdance";
        if (lower.find("ivonne") != std::string::npos) return "Ivonne Latin Dance";
        if (lower.find("moon") != std::string::npos) return "Moonwalk Dancer";
        if (lower.find("avatarsample_b") != std::string::npos) return "Graceful Dance (Sample B)";
        if (lower.find("kachujin") != std::string::npos) return "Kachujin Chicken Dance";
        if (lower.find("dragon") != std::string::npos) return "Black Dragon (Animated)";
        if (lower.find("kitten") != std::string::npos) return "Kitten Monk Kung-Fu";
        if (lower.find("mint") != std::string::npos) return "Mint Swimsuit Animated";
        if (lower.find("solar") != std::string::npos) return "Solar System Orbit";
        if (lower.find("seoul_city_pbr") != std::string::npos) return "Seoul City (PBR Street)";
        if (lower.find("seoul_city_original") != std::string::npos) return "Seoul City (Original)";

        std::string base = filename;
        size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        return base;
    }

    static bool IsImageFile(const DiscoveredModelFile& item) {
        return SurrealImage3D::IsImageExt(item.extension);
    }

    void UpdateCatalogFilter() {
        activeCatalogList.clear();
        for (const auto& item : folderModels) {
            const bool isImg = IsImageFile(item);
            if (activeCategory == CatalogCategory::ALL) {
                activeCatalogList.push_back(item);
            } else if (activeCategory == CatalogCategory::PHOTO3D) {
                if (isImg) activeCatalogList.push_back(item);
            } else if (isImg) {
                continue; // pictures only show up in PHOTO->3D and ALL
            } else if (activeCategory == CatalogCategory::DANCERS) {
                if (IsDanceModel(item.filename)) activeCatalogList.push_back(item);
            } else if (activeCategory == CatalogCategory::AVATARS) {
                if (IsAvatarModel(item.filename) && !IsDanceModel(item.filename)) activeCatalogList.push_back(item);
            } else if (activeCategory == CatalogCategory::WORLDS_PROPS) {
                if (IsWorldOrPropModel(item.filename) || item.extension == ".obj") activeCatalogList.push_back(item);
            }
        }
        if (activeCatalogList.empty() && activeCategory != CatalogCategory::PHOTO3D) {
            activeCatalogList = folderModels;
        }
        catalogPage = 0;
    }

    static const char* CategoryLabel(CatalogCategory c, bool longName) {
        switch (c) {
            case CatalogCategory::DANCERS:      return longName ? "DANCING MODELS" : "DANCERS";
            case CatalogCategory::AVATARS:      return longName ? "AION AVATARS" : "AVATARS";
            case CatalogCategory::WORLDS_PROPS: return longName ? "WORLDS & PROPS" : "WORLDS";
            case CatalogCategory::PHOTO3D:      return longName ? "PHOTO -> 3D (Bilder)" : "PHOTO3D";
            default:                            return longName ? "ALL MODELS" : "ALL";
        }
    }

    void CycleCategory() {
        switch (activeCategory) {
            case CatalogCategory::DANCERS: activeCategory = CatalogCategory::AVATARS; break;
            case CatalogCategory::AVATARS: activeCategory = CatalogCategory::WORLDS_PROPS; break;
            case CatalogCategory::WORLDS_PROPS: activeCategory = CatalogCategory::PHOTO3D; break;
            case CatalogCategory::PHOTO3D: activeCategory = CatalogCategory::ALL; break;
            case CatalogCategory::ALL: activeCategory = CatalogCategory::DANCERS; break;
        }
        UpdateCatalogFilter();
        std::string msg = std::string("[*] Category: ") + CategoryLabel(activeCategory, true) + " (" + std::to_string(activeCatalogList.size()) + " files)";
        if (activeCategory == CatalogCategory::PHOTO3D && activeCatalogList.empty()) {
            msg += " - lege PNG/JPG nach assets/models/photo3d/ und druecke [F5]";
        }
        SetStatus(msg, activeCategory == CatalogCategory::PHOTO3D ? 5.0f : 3.0f);
    }

    int GetTotalCatalogPages() const {
        if (activeCatalogList.empty()) return 1;
        return (static_cast<int>(activeCatalogList.size()) + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    }

    void CatalogNextPage() {
        int total = GetTotalCatalogPages();
        catalogPage = (catalogPage + 1) % total;
        SetStatus("[*] Catalog Page " + std::to_string(catalogPage + 1) + "/" + std::to_string(total), 2.0f);
    }

    void CatalogPrevPage() {
        int total = GetTotalCatalogPages();
        catalogPage = (catalogPage - 1 + total) % total;
        SetStatus("[*] Catalog Page " + std::to_string(catalogPage + 1) + "/" + std::to_string(total), 2.0f);
    }

    // Shared layout for catalog drawing AND mouse hit-testing (single source of truth)
    struct CatalogGeom {
        int x, y, w, h;
        int listY, navY, presetY;
        int pageStart, pageEnd, pageCount, rows;
    };

    CatalogGeom GetCatalogGeom(int screenW, int screenH) {
        CatalogGeom g;
        int totalPages = GetTotalCatalogPages();
        if (catalogPage >= totalPages) catalogPage = totalPages - 1;
        if (catalogPage < 0) catalogPage = 0;
        g.pageStart = catalogPage * ITEMS_PER_PAGE;
        g.pageEnd = std::min(g.pageStart + ITEMS_PER_PAGE, static_cast<int>(activeCatalogList.size()));
        g.pageCount = std::max(0, g.pageEnd - g.pageStart);
        g.rows = std::max(1, g.pageCount);
        g.w = 640;
        g.h = 220 + g.rows * 22;
        g.x = (screenW - g.w) / 2;
        g.y = (screenH - g.h) / 2;
        g.listY = g.y + 58;
        g.navY = g.listY + g.rows * 22 + 4;
        g.presetY = g.navY + 42; // first preset row (after nav bar + header)
        return g;
    }

    // Returns true if a model/preset was picked and placement started.
    // Keys: 1-8 = item on current page, 9 = Ramp, 0 = Monolith, M = Mia clone.
    bool PickCatalogEntry(int slot, const std::shared_ptr<SurrealMesh>& avatarMesh) {
        // slot: 1..8 page items, 9/10/11 = builtin presets 1/2/4
        if (slot >= 1 && slot <= ITEMS_PER_PAGE) {
            int idx = catalogPage * ITEMS_PER_PAGE + (slot - 1);
            if (idx >= static_cast<int>(activeCatalogList.size())) return false;
            SelectFolderModel(idx, avatarMesh);
        } else if (slot == 9) {
            SelectBuiltinPreset(1, avatarMesh);
        } else if (slot == 10) {
            SelectBuiltinPreset(2, avatarMesh);
        } else if (slot == 11) {
            SelectBuiltinPreset(4, avatarMesh);
        } else {
            return false;
        }
        return isPlacingModel;
    }

    void RefreshModelFolder() {
        folderModels.clear();
        catalogPage = 0;
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
        UpdateCatalogFilter();
    }

    void CycleModelFolder() {
        activeFolderIndex = (activeFolderIndex + 1) % modelFolders.size();
        catalogPage = 0;
        folderModels = SurrealModelLoader::ScanFolder(modelFolders[activeFolderIndex]);
        UpdateCatalogFilter();
        SetStatus("[*] Switched 3D Folder: " + modelFolders[activeFolderIndex] + " (" + std::to_string(folderModels.size()) + " models found)");
    }

    void ToggleModelCatalog() {
        showModelCatalog = !showModelCatalog;
        if (showModelCatalog) {
            RefreshModelFolder();
            std::string catName = CategoryLabel(activeCategory, false);
            SetStatus("[*] 3D Model Importer [" + catName + "]: " + std::to_string(activeCatalogList.size()) + " items. Choose [1]-[8], [T] Category, [,] [.] Page", 4.0f);
        }
    }

    void SelectFolderModel(int fileIndex, const std::shared_ptr<SurrealMesh>& avatarMesh = nullptr) {
        if (fileIndex < 0 || fileIndex >= static_cast<int>(activeCatalogList.size())) return;
        const auto& file = activeCatalogList[fileIndex];

        placementScale = Vec3(1.0f, 1.0f, 1.0f);
        placementYaw = 0.0f;
        placementColor = ColorRGB(1.0f, 1.0f, 1.0f); // Default neutral albedo for textured models
        placementMetallic = 0.25f;
        placementRoughness = 0.45f;
        placementSkeletal = nullptr;

        bool loaded = false;
        bool isPhoto = IsImageFile(file);
        if (isPhoto) {
            Image3DOptions opt;
            Image3DInfo info;
            placementMesh = SurrealImage3D::BuildFromFile(file.fullPath, opt, &info);
            loaded = (placementMesh != nullptr);
            placementMetallic = 0.05f;   // matte, so the picture colours stay faithful
            placementRoughness = 0.85f;
            if (!loaded) {
                SetStatus("[-] Photo->3D: " + info.error, 5.0f);
                return;
            }
        } else if (file.extension == ".obj") {
            placementColor = colorPalette[currentPaletteIdx % colorPalette.size()];
            loaded = SurrealModelLoader::LoadCustomFile(file.fullPath, placementMesh, placementColor);
        } else if (file.extension == ".gltf" || file.extension == ".glb") {
            auto inst = std::make_shared<SkeletalMeshInstance>();
            loaded = SurrealSkeletalLoader::LoadSkeletalModel(file.fullPath, *inst);
            if (loaded) {
                placementMesh = inst->activeDeformedMesh;
                placementSkeletal = inst;
            }
        }

        if (!loaded || !placementMesh) {
            SetStatus("[-] Failed to load 3D model file: " + file.filename, 4.0f);
            return;
        }

        // Clean name without extension
        std::string baseName = GetCleanName(file.filename);
        placementName = baseName + "_" + std::to_string(entities.size() + 1);

        // Normalize scale
        placementMesh->CalculateBounds();
        Vec3 ext = placementMesh->worldBounds.Extents() * 2.0f;
        float maxDim = std::max({ext.x, ext.y, ext.z});
        float scaleFactor = 1.0f;
        if (maxDim > 0.001f) {
            bool isEnvironment = (baseName.find("City") != std::string::npos ||
                                  baseName.find("Seoul") != std::string::npos ||
                                  baseName.find("Map") != std::string::npos ||
                                  baseName.find("World") != std::string::npos);
            if (isPhoto) {
                scaleFactor = 1.0f; // Photo->3D meshes are already built at ~1.8 m
            } else if (isEnvironment) {
                scaleFactor = 1.0f; // Keep real-world 1:1 scale for cities & environments!
            } else if (IsDanceModel(file.filename) || IsAvatarModel(file.filename)) {
                // Humanoid characters: normalize height to ~1.80m tall
                float charHeight = placementMesh->worldBounds.max.y - placementMesh->worldBounds.min.y;
                if (charHeight > 0.0001f) {
                    scaleFactor = 1.80f / charHeight;
                } else {
                    scaleFactor = 1.8f / maxDim;
                }
            } else if (maxDim > 50.0f) {
                scaleFactor = 2.0f / maxDim;
            } else if (maxDim < 0.2f) {
                scaleFactor = 1.5f / maxDim;
            } else {
                scaleFactor = 1.8f / maxDim;
            }
        }
        placementScale = Vec3(scaleFactor, scaleFactor, scaleFactor);
        activePreset = ModelPresetType::CUSTOM_FILE;

        showModelCatalog = false;
        isPlacingModel = true;
        SetStatus("[+] PLACING: " + placementName + " | Move Mouse, Left-Click to drop, [R] Rotate, [ESC] Cancel", 5.0f);
    }

    void SelectBuiltinPreset(int presetIdx, const std::shared_ptr<SurrealMesh>& avatarMesh = nullptr) {
        placementScale = Vec3(1.0f, 1.0f, 1.0f);
        placementYaw = 0.0f;
        placementColor = colorPalette[currentPaletteIdx % colorPalette.size()];
        placementMetallic = 0.45f;
        placementRoughness = 0.35f;

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

        showModelCatalog = false;
        isPlacingModel = true;
        SetStatus("[+] PLACING: " + placementName + " | Move Mouse, Left-Click to drop, [R] Rotate, [ESC] Cancel", 5.0f);
    }

    void SelectModelPreset(int choice, const std::shared_ptr<SurrealMesh>& avatarMesh = nullptr) {
        if (choice >= 1 && choice <= static_cast<int>(folderModels.size())) {
            SelectFolderModel(choice - 1, avatarMesh);
        } else {
            int presetIdx = choice - static_cast<int>(folderModels.size());
            SelectBuiltinPreset(presetIdx, avatarMesh);
        }
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
        bool isEnv = (placementName.find("City") != std::string::npos ||
                      placementName.find("Seoul") != std::string::npos ||
                      placementName.find("Map") != std::string::npos ||
                      placementName.find("World") != std::string::npos);
        ed.isStaticTerrain = isEnv;
        ed.isCharacter = (activePreset == ModelPresetType::AVATAR_CLONE ||
                          placementName.find("Avatar") != std::string::npos);

        entities.push_back(ed);
        selectedIndex = static_cast<int>(entities.size() - 1);

        if (placementSkeletal) {
            animatedEntities[newEntity] = placementSkeletal;
            placementSkeletal = nullptr;
        }

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
        if (!ent || ent->name == "CyberGridFloor") return;

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
        if (!ent || ent->name == "CyberGridFloor" || ent->isCharacter) return;

        ent->scale = ent->scale * factor;
        ent->scale.x = std::max(0.01f, ent->scale.x);
        ent->scale.y = std::max(0.01f, ent->scale.y);
        ent->scale.z = std::max(0.01f, ent->scale.z);

        if (world.transforms.find(ent->id) != world.transforms.end()) {
            world.transforms[ent->id].scale = ent->scale;
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
        if (!ent || ent->name == "CyberGridFloor" || ent->isCharacter) return;

        ent->color = newColor;
        if (world.meshRenderers.find(ent->id) != world.meshRenderers.end()) {
            world.meshRenderers[ent->id].material.albedo = ent->color;
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
        if (!ent || ent->name == "CyberGridFloor") return;

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
        if (!ent || ent->name == "CyberGridFloor" || ent->isCharacter) {
            SetStatus("[!] Cannot delete core scene entity!");
            return;
        }

        std::string delName = ent->name;
        animatedEntities.erase(ent->id);
        world.DestroyEntity(ent->id);
        entities.erase(entities.begin() + selectedIndex);
        if (selectedIndex >= static_cast<int>(entities.size())) {
            selectedIndex = static_cast<int>(entities.size() - 1);
        }

        SyncObstacles(obstacles);
        SetStatus("[-] Deleted Entity: " + delName);
    }

    void UpdateAnimations(float dt) {
        for (auto& pair : animatedEntities) {
            if (pair.second) pair.second->Update(dt);
        }
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
            CatalogGeom g = GetCatalogGeom(screenW, screenH);
            int totalPages = GetTotalCatalogPages();
            int diagX = g.x, diagY = g.y;

            SurrealHUD::DrawPanel(fb, screenW, screenH, g.x, g.y, g.w, g.h, 0x050A18, 0.96f);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, diagY + 16, "=== 3D MODEL FOLDER IMPORTER & BROWSER ===", 0xFF00FFCC, 1);

            char folderBuf[160];
            std::string dispFolder = modelFolders[activeFolderIndex % modelFolders.size()];
            std::string catName = CategoryLabel(activeCategory, true);
            std::snprintf(folderBuf, sizeof(folderBuf), "Category: [%s] (Press [T]) | Folder: [%s] (%zu files) | Page %d/%d",
                          catName.c_str(), dispFolder.c_str(), activeCatalogList.size(), catalogPage + 1, totalPages);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, diagY + 34, folderBuf, 0xFF88AABB, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, diagY + 46, "-------------------------------------------------------------", 0xFF335577, 1);

            int yPos = g.listY;
            for (int i = g.pageStart; i < g.pageEnd; ++i) {
                int keyNum = (i - g.pageStart) + 1;
                const auto& item = activeCatalogList[i];
                char lineBuf[160];
                float kb = item.sizeBytes / 1024.0f;
                std::string cleanName = GetCleanName(item.filename);
                if (cleanName.length() > 28) cleanName = cleanName.substr(0, 26) + "..";

                uint32_t itemCol = 0xFF00E5FF;
                const char* tag = "[PROP]";
                if (IsImageFile(item)) {
                    itemCol = 0xFF66FF66; // Photo -> 3D
                    tag = "[PHOTO]";
                } else if (IsDanceModel(item.filename)) {
                    itemCol = 0xFFFF1493; // Deep / Hot Pink
                    tag = "[DANCE]";
                } else if (IsAvatarModel(item.filename)) {
                    itemCol = 0xFFFF00DD; // Neon Magenta
                    tag = "[AVATAR]";
                } else if (IsWorldOrPropModel(item.filename)) {
                    itemCol = 0xFFFFCC00; // Radiant Gold
                    tag = "[WORLD]";
                }

                std::snprintf(lineBuf, sizeof(lineBuf), "[%d] %-7s %-28s %6.0f KB",
                              keyNum, tag, cleanName.c_str(), kb);
                SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos, lineBuf, itemCol, 1);
                yPos += 22;
            }
            if (g.pageCount == 0) {
                SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, yPos, "[ No 3D models found in this category ]", 0xFF888888, 1);
            }

            char pageNavBuf[128];
            std::snprintf(pageNavBuf, sizeof(pageNavBuf), "[< PREV ( , )]          Page %d / %d          [NEXT ( . ) >]",
                          catalogPage + 1, totalPages);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, g.navY, pageNavBuf, 0xFF00FF88, 1);

            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, g.presetY - 20, "--- Engine Built-in Presets ---", 0xFF667788, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, g.presetY,      "[9] Parkour Ramp (Walkable Wedge Slope)", 0xFFFF8800, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, g.presetY + 20, "[0] Cyber Monolith (Sci-Fi Beveled Obelisk)", 0xFFFFCC00, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, g.presetY + 40, "[M] Mia Avatar Clone (glTF Rigged Character)", 0xFFFF007F, 1);
            SurrealHUD::DrawString(fb, screenW, screenH, diagX + 20, g.presetY + 66,
                                   "[1]-[8] Place NPC | [SH+1]-[8] Play As Avatar | [T] Category | [,][.] Page | [ESC] Close", 0xFF00FF88, 1);
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
