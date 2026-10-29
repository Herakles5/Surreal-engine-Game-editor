#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - DATA-ORIENTED ENTITY COMPONENT SYSTEM (ECS)
 * Zero-overhead cache-coherent linear components for bare-metal performance
 * Integrates AION Cognitive World Mind directly into the scene entities
 * ============================================================================
 */

#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include "surreal_meshlet.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

namespace Surreal {

using EntityID = uint32_t;
constexpr EntityID INVALID_ENTITY = 0xFFFFFFFF;

// Component Types
struct TransformComponent {
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    TransformComponent()
        : position(0, 0, 0), rotation(0, 0, 0, 1.0f), scale(1.0f, 1.0f, 1.0f) {}

    Mat4 GetWorldMatrix() const {
        return Mat4::Translation(position) * Mat4::FromQuat(rotation) * Mat4::Scaling(scale);
    }
};

struct RigidBodyComponent {
    Vec3 velocity;
    Vec3 acceleration;
    float mass;
    float restitution; // Bounciness
    float friction;
    bool isStatic;

    RigidBodyComponent()
        : velocity(0, 0, 0), acceleration(0, -9.81f, 0), mass(1.0f), restitution(0.5f), friction(0.3f), isStatic(false) {}
};

struct ColliderComponent {
    enum Type { SPHERE, BOX, CAPSULE } type;
    Vec3 centerOffset;
    float radius;
    Vec3 halfExtents;

    ColliderComponent()
        : type(SPHERE), centerOffset(0,0,0), radius(1.0f), halfExtents(1.0f, 1.0f, 1.0f) {}
};

struct MeshRendererComponent {
    std::shared_ptr<SurrealMesh> mesh;
    PBRMaterial material;
    bool castShadows;
    bool receiveShadows;

    MeshRendererComponent()
        : mesh(nullptr), material(), castShadows(true), receiveShadows(true) {}
};

/**
 * Aion Cognitive World-Mind Component
 * Grants dynamic cognitive behavior, voice triggers, and evolutionary reaction to entities
 */
struct AionMindComponent {
    std::string personalityId;
    std::string currentObjective;
    float alertness;          // [0.0 .. 1.0]
    float emotionalValence;   // [-1.0 .. +1.0]
    uint64_t lastThoughtTick;
    bool canSpeak;

    AionMindComponent()
        : personalityId("Aion_Vanguard"),
          currentObjective("Explore & Protect"),
          alertness(0.5f),
          emotionalValence(0.0f),
          lastThoughtTick(0),
          canSpeak(true) {}
};

/**
 * Surreal World / Scene Graph
 */
class SurrealWorld {
public:
    std::vector<EntityID> entities;
    std::unordered_map<EntityID, TransformComponent> transforms;
    std::unordered_map<EntityID, RigidBodyComponent> rigidbodies;
    std::unordered_map<EntityID, ColliderComponent> colliders;
    std::unordered_map<EntityID, MeshRendererComponent> meshRenderers;
    std::unordered_map<EntityID, AionMindComponent> aionMinds;

    DirectionalLight sunLight;
    std::vector<PointLight> pointLights;
    ColorRGB ambientLight;

    SurrealWorld() : nextEntityId(1), ambientLight(0.08f, 0.08f, 0.12f) {
        sunLight.direction = Vec3(0.577f, 0.577f, 0.577f).Normalized();
        sunLight.color = ColorRGB(1.0f, 0.98f, 0.92f);
        sunLight.intensity = 1.0f;
    }

    EntityID CreateEntity(const std::string& name = "Entity") {
        EntityID id = nextEntityId++;
        entities.push_back(id);
        transforms[id] = TransformComponent();
        return id;
    }

    void DestroyEntity(EntityID id) {
        transforms.erase(id);
        rigidbodies.erase(id);
        colliders.erase(id);
        meshRenderers.erase(id);
        aionMinds.erase(id);
        for (auto it = entities.begin(); it != entities.end(); ++it) {
            if (*it == id) {
                entities.erase(it);
                break;
            }
        }
    }

    void UpdatePhysics(float dt) {
        for (auto& pair : rigidbodies) {
            EntityID id = pair.first;
            RigidBodyComponent& rb = pair.second;
            if (rb.isStatic) continue;

            auto itT = transforms.find(id);
            if (itT == transforms.end()) continue;
            TransformComponent& t = itT->second;

            // Simple symplectic Euler integration
            rb.velocity = rb.velocity + (rb.acceleration * dt);
            t.position = t.position + (rb.velocity * dt);

            // Ground floor plane bounce (y = 0)
            if (t.position.y < 0.0f) {
                t.position.y = 0.0f;
                rb.velocity.y = -rb.velocity.y * rb.restitution;
                rb.velocity.x *= (1.0f - rb.friction * dt);
                rb.velocity.z *= (1.0f - rb.friction * dt);
            }
        }
    }

private:
    EntityID nextEntityId;
};

} // namespace Surreal

