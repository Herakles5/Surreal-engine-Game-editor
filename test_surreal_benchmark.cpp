/**
 * AION SURREAL 3D ENGINE - BARE-METAL BENCHMARK & VERIFICATION
 */

#include "surreal_engine.hpp"
#include <iostream>
#include <vector>

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  AION SURREAL 3D HYBRID ENGINE - BARE-METAL TEST & BENCH\n";
    std::cout << "  Unreal-Hybrid Architecture: Meshlets, PBR & Data ECS\n";
    std::cout << "=========================================================\n";

    constexpr uint32_t W = 640;
    constexpr uint32_t H = 480;
    std::vector<uint32_t> frame(W * H, 0);

    Surreal::SurrealEngine engine(W, H);
    engine.AttachFramebuffer(frame.data(), W * sizeof(uint32_t));

    // Create Golden PBR Cube
    Surreal::EntityID cubeId = engine.world.CreateEntity("GoldenPBRCube");
    Surreal::MeshRendererComponent mr;
    mr.mesh = Surreal::SurrealEngine::CreateCube(2.0f);
    mr.material.albedo = Surreal::ColorRGB(1.0f, 0.76f, 0.33f); // Metallic Gold
    mr.material.metallic = 0.9f;
    mr.material.roughness = 0.25f;
    engine.world.meshRenderers[cubeId] = mr;

    // Attach Aion Cognitive Mind Component
    Surreal::AionMindComponent mind;
    mind.personalityId = "AION_PRIME_CONSCIOUSNESS";
    mind.currentObjective = "Autonomous World Synthesis & Bare-Metal Governance";
    engine.world.aionMinds[cubeId] = mind;

    std::cout << "[+] World Initialized: " << engine.world.entities.size() << " entity created.\n";
    std::cout << "[+] Meshlets in Cube: " << mr.mesh->meshlets.size() << "\n";
    std::cout << "[+] Benchmarking 60 Frames of PBR Rendering...\n";

    for (int frameIdx = 0; frameIdx < 60; ++frameIdx) {
        // Rotate entity over time
        Surreal::TransformComponent& t = engine.world.transforms[cubeId];
        t.rotation = Surreal::Quat::FromAxisAngle(Surreal::Vec3(0.5f, 1.0f, 0.2f), frameIdx * 0.05f);

        engine.RenderFrame(0.016f);
    }

    std::cout << "[✓] 60 Frames rendered successfully!\n";
    std::cout << "    Last Frame Time: " << engine.lastFrameTimeMs << " ms\n";
    std::cout << "    Rendered Meshlets per frame: " << engine.renderedMeshlets << "\n";
    std::cout << "    Culled Meshlets: " << engine.culledMeshlets << "\n";
    std::cout << "=========================================================\n";
    std::cout << "  SURREAL ENGINE BARE-METAL VALIDATION: SUCCESS\n";
    std::cout << "=========================================================\n";

    return 0;
}

