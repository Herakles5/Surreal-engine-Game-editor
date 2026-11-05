#include "surreal_animation.hpp"
#include <iostream>

int main() {
    Surreal::SkeletalMeshInstance character;
    const std::string modelPath = "/root/coding/MeinOS/avatar/scene.gltf";
    if (!Surreal::SurrealSkeletalLoader::LoadSkeletalModel(modelPath, character)) {
        std::cerr << "Failed to load\n";
        return 1;
    }

    std::cout << "Joints count: " << character.joints.size() << "\n";
    std::cout << "Bind verts count: " << character.bindVertices.size() << "\n";

    // Check bind vertex 0
    std::cout << "Bind vert 0 pos: ("
              << character.bindVertices[0].position.x << ", "
              << character.bindVertices[0].position.y << ", "
              << character.bindVertices[0].position.z << ")\n";

    // Test skinning update at t=0
    character.Update(0.0f);

    std::cout << "Skinned vert 0 pos: ("
              << character.activeDeformedMesh->vertices[0].position.x << ", "
              << character.activeDeformedMesh->vertices[0].position.y << ", "
              << character.activeDeformedMesh->vertices[0].position.z << ")\n";

    // Check bounding box of skinned vertices
    Surreal::AABB bounds;
    for (const auto& v : character.activeDeformedMesh->vertices) {
        bounds.Expand(v.position);
    }
    std::cout << "Skinned Bounds: Min=(" << bounds.min.x << ", " << bounds.min.y << ", " << bounds.min.z << ") -> Max=("
              << bounds.max.x << ", " << bounds.max.y << ", " << bounds.max.z << ")\n";

    return 0;
}

