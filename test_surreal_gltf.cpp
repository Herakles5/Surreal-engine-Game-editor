/**
 * ============================================================================
 * AION SURREAL ENGINE - glTF 3D MESH LOADER & PBR DOJO VERIFICATION
 * Tests loading real 3D models (Avatar/Character) into Surreal Meshlets
 * ============================================================================
 */

#include "surreal_engine.hpp"
#include "surreal_gltf.hpp"
#include <iostream>
#include <chrono>

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  AION SURREAL 3D ENGINE - glTF 3D MESH LOADER & PBR DOJO\n";
    std::cout << "  Hybrid Linux & CosmOS Bare-Metal Model Pipeline\n";
    std::cout << "=========================================================\n";

    const std::string modelPath = "/root/coding/MeinOS/avatar/scene.gltf";
    std::cout << "[*] Loading 3D Character Model: " << modelPath << "\n";

    auto tLoadStart = std::chrono::high_resolution_clock::now();

    std::vector<Surreal::GLTFModelNode> modelNodes;
    bool success = Surreal::SurrealGLTFLoader::LoadModel(modelPath, modelNodes);

    auto tLoadEnd = std::chrono::high_resolution_clock::now();
    float loadTimeMs = std::chrono::duration<float, std::milli>(tLoadEnd - tLoadStart).count();

    if (!success) {
        std::cerr << "[-] Error: Failed to load glTF model at " << modelPath << "\n";
        return 1;
    }

    std::cout << "[✓] glTF Loaded successfully in " << loadTimeMs << " ms!\n";
    std::cout << "    Primitives / Sub-Meshes loaded: " << modelNodes.size() << "\n";

    uint64_t totalVertices = 0;
    uint64_t totalTriangles = 0;
    uint64_t totalMeshlets = 0;

    for (size_t i = 0; i < modelNodes.size(); ++i) {
        const auto& node = modelNodes[i];
        totalVertices += node.mesh->vertices.size();
        totalTriangles += node.mesh->indices.size() / 3;
        totalMeshlets += node.mesh->meshlets.size();

        if (i < 5) {
            std::cout << "    [Node " << i << "]: " << node.name
                      << " | Vertices: " << node.mesh->vertices.size()
                      << " | Triangles: " << node.mesh->indices.size() / 3
                      << " | Meshlets: " << node.mesh->meshlets.size()
                      << " | Metallic: " << node.material.metallic
                      << " | Roughness: " << node.material.roughness << "\n";
        }
    }

    std::cout << "    -------------------------------------------------\n";
    std::cout << "    TOTAL MODEL VERTICES:  " << totalVertices << "\n";
    std::cout << "    TOTAL MODEL TRIANGLES: " << totalTriangles << "\n";
    std::cout << "    TOTAL SURREAL MESHLETS:" << totalMeshlets << "\n";
    std::cout << "=========================================================\n";

    // Setup Engine & PBR Dojo World
    constexpr uint32_t W = 800;
    constexpr uint32_t H = 600;
    std::vector<uint32_t> frame(W * H, 0);

    Surreal::SurrealEngine engine(W, H);
    engine.AttachFramebuffer(frame.data(), W * sizeof(uint32_t));

    // Position camera to frame the character avatar
    engine.cameraPos = Surreal::Vec3(0, 1.2f, 3.0f);
    engine.cameraTarget = Surreal::Vec3(0, 0.9f, 0);

    // Setup warm atmospheric PBR lighting
    engine.world.sunLight.direction = Surreal::Vec3(0.5f, 0.8f, 0.6f).Normalized();
    engine.world.sunLight.color = Surreal::ColorRGB(1.0f, 0.95f, 0.85f);
    engine.world.sunLight.intensity = 1.3f;
    engine.world.ambientLight = Surreal::ColorRGB(0.12f, 0.12f, 0.18f);

    // Spawn glTF model entities into the world
    for (size_t i = 0; i < modelNodes.size(); ++i) {
        Surreal::EntityID ent = engine.world.CreateEntity(modelNodes[i].name);
        Surreal::TransformComponent& t = engine.world.transforms[ent];
        t.position = Surreal::Vec3(0, 0, 0);
        t.scale = Surreal::Vec3(1.0f, 1.0f, 1.0f);

        Surreal::MeshRendererComponent mr;
        mr.mesh = modelNodes[i].mesh;
        mr.material = modelNodes[i].material;
        engine.world.meshRenderers[ent] = mr;
    }

    // Attach Aion Cognitive Mind to the Primary Avatar Node
    if (!modelNodes.empty()) {
        Surreal::AionMindComponent avatarMind;
        avatarMind.personalityId = "AION_HYBRID_AVATAR_PRIME";
        avatarMind.currentObjective = "PBR Dojo Spatial Cognition & Bare-Metal Interaction";
        engine.world.aionMinds[1] = avatarMind;
    }

    std::cout << "[+] Rendering Character Model in PBR Dojo...\n";

    auto tRenderStart = std::chrono::high_resolution_clock::now();
    engine.RenderFrame(0.016f);
    auto tRenderEnd = std::chrono::high_resolution_clock::now();
    float renderTimeMs = std::chrono::duration<float, std::milli>(tRenderEnd - tRenderStart).count();

    std::cout << "[✓] Frame Rendered Successfully!\n";
    std::cout << "    Render Time: " << renderTimeMs << " ms ("
              << (1000.0f / std::max(renderTimeMs, 0.001f)) << " FPS)\n";
    std::cout << "    Rendered Meshlets: " << engine.renderedMeshlets << "\n";
    std::cout << "    Culled Meshlets:   " << engine.culledMeshlets << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  glTF 3D CHARACTER PBR INTEGRATION: SUCCESSFUL!\n";
    std::cout << "=========================================================\n";

    return 0;
}

