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
    std::unordered_map<EntityID, std::string> entityNames;
    std::unordered_map<EntityID, TransformComponent> transforms;
    std::unordered_map<EntityID, RigidBodyComponent> rigidbodies;
    std::unordered_map<EntityID, ColliderComponent> colliders;
    std::unordered_map<EntityID, MeshRendererComponent> meshRenderers;
    std::unordered_map<EntityID, AionMindComponent> aionMinds;

    DirectionalLight sunLight;
    std::vector<PointLight> pointLights;
    ColorRGB ambientLight;

    SurrealWorld() : ambientLight(0.08f, 0.08f, 0.12f), nextEntityId(1) {
        sunLight.direction = Vec3(0.577f, 0.577f, 0.577f).Normalized();
        sunLight.color = ColorRGB(1.0f, 0.98f, 0.92f);
        sunLight.intensity = 1.0f;
    }

    EntityID CreateEntity(const std::string& name = "Entity") {
        EntityID id = nextEntityId++;
        entities.push_back(id);
        entityNames[id] = name;
        transforms[id] = TransformComponent();
        return id;
    }

    void DestroyEntity(EntityID id) {
        entityNames.erase(id);
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
        if (dt <= 0.0f) return;
        dt = std::min(dt, 0.05f);

        for (auto& pair : rigidbodies) {
            EntityID id = pair.first;
            RigidBodyComponent& rb = pair.second;
            if (rb.isStatic) continue;

            auto itT = transforms.find(id);
            if (itT == transforms.end()) continue;
            TransformComponent& t = itT->second;

            // Symplectic Euler integration
            rb.velocity = rb.velocity + (rb.acceleration * dt);
            t.position = t.position + (rb.velocity * dt);

            float bottomOffset = 0.0f;
            auto itCol = colliders.find(id);
            if (itCol != colliders.end()) {
                const ColliderComponent& c = itCol->second;
                float maxScale = std::max({fabsf(t.scale.x), fabsf(t.scale.y), fabsf(t.scale.z)});
                if (c.type == ColliderComponent::SPHERE) {
                    bottomOffset = std::max(0.0f, c.radius * maxScale - c.centerOffset.y * t.scale.y);
                } else if (c.type == ColliderComponent::CAPSULE) {
                    float sy = std::max(c.halfExtents.y * fabsf(t.scale.y), c.radius * maxScale);
                    bottomOffset = std::max(0.0f, sy - c.centerOffset.y * t.scale.y);
                } else {
                    bottomOffset = std::max(0.0f, c.halfExtents.y * fabsf(t.scale.y) - c.centerOffset.y * t.scale.y);
                }
            }

            // Ground floor plane bounce (y = 0)
            if (t.position.y < bottomOffset) {
                t.position.y = bottomOffset;
                if (rb.velocity.y < 0.0f) {
                    rb.velocity.y = -rb.velocity.y * rb.restitution;
                }
                float damp = std::max(0.0f, 1.0f - rb.friction * dt);
                rb.velocity.x *= damp;
                rb.velocity.z *= damp;
            }
        }

        // Pairwise collider collision detection & impulse resolution
        std::vector<EntityID> colliderIds;
        colliderIds.reserve(colliders.size());
        for (const auto& kv : colliders) {
            if (transforms.find(kv.first) != transforms.end()) {
                colliderIds.push_back(kv.first);
            }
        }

        auto getInvMass = [&](EntityID id, RigidBodyComponent*& outRb) -> float {
            auto it = rigidbodies.find(id);
            if (it == rigidbodies.end() || it->second.isStatic || it->second.mass <= 1e-5f) {
                outRb = nullptr;
                return 0.0f;
            }
            outRb = &it->second;
            return 1.0f / it->second.mass;
        };

        auto getWorldCenter = [&](EntityID id, const ColliderComponent& c) -> Vec3 {
            const TransformComponent& t = transforms[id];
            return t.position + Vec3(c.centerOffset.x * t.scale.x, c.centerOffset.y * t.scale.y, c.centerOffset.z * t.scale.z);
        };

        auto getScaledBoxExtents = [&](EntityID id, const ColliderComponent& c) -> Vec3 {
            const TransformComponent& t = transforms[id];
            if (c.type == ColliderComponent::SPHERE) {
                float r = c.radius * std::max({fabsf(t.scale.x), fabsf(t.scale.y), fabsf(t.scale.z)});
                return Vec3(r, r, r);
            }
            if (c.type == ColliderComponent::CAPSULE) {
                float r = c.radius * std::max(fabsf(t.scale.x), fabsf(t.scale.z));
                float hy = std::max(r, c.halfExtents.y * fabsf(t.scale.y));
                return Vec3(r, hy, r);
            }
            return Vec3(c.halfExtents.x * fabsf(t.scale.x),
                        c.halfExtents.y * fabsf(t.scale.y),
                        c.halfExtents.z * fabsf(t.scale.z));
        };

        for (size_t i = 0; i < colliderIds.size(); ++i) {
            EntityID idA = colliderIds[i];
            RigidBodyComponent* rbA = nullptr;
            float invMassA = getInvMass(idA, rbA);
            const ColliderComponent& colA = colliders[idA];
            TransformComponent& trA = transforms[idA];

            for (size_t j = i + 1; j < colliderIds.size(); ++j) {
                EntityID idB = colliderIds[j];
                RigidBodyComponent* rbB = nullptr;
                float invMassB = getInvMass(idB, rbB);
                float totalInvMass = invMassA + invMassB;
                if (totalInvMass <= 0.0f) continue;

                const ColliderComponent& colB = colliders[idB];
                TransformComponent& trB = transforms[idB];

                Vec3 cA = getWorldCenter(idA, colA);
                Vec3 cB = getWorldCenter(idB, colB);
                Vec3 normal(0.0f, 1.0f, 0.0f);
                float penetration = 0.0f;
                bool hit = false;

                if (colA.type == ColliderComponent::SPHERE && colB.type == ColliderComponent::SPHERE) {
                    float rA = colA.radius * std::max({fabsf(trA.scale.x), fabsf(trA.scale.y), fabsf(trA.scale.z)});
                    float rB = colB.radius * std::max({fabsf(trB.scale.x), fabsf(trB.scale.y), fabsf(trB.scale.z)});
                    Vec3 diff = cB - cA;
                    float distSq = diff.LengthSq();
                    float rSum = rA + rB;
                    if (distSq < rSum * rSum) {
                        float dist = sqrtf(distSq);
                        normal = (dist > 1e-6f) ? (diff * (1.0f / dist)) : Vec3(0.0f, 1.0f, 0.0f);
                        penetration = rSum - dist;
                        hit = true;
                    }
                } else if (colA.type == ColliderComponent::SPHERE || colB.type == ColliderComponent::SPHERE) {
                    bool aIsSphere = (colA.type == ColliderComponent::SPHERE);
                    const Vec3& sCenter = aIsSphere ? cA : cB;
                    float sRadius = aIsSphere
                        ? colA.radius * std::max({fabsf(trA.scale.x), fabsf(trA.scale.y), fabsf(trA.scale.z)})
                        : colB.radius * std::max({fabsf(trB.scale.x), fabsf(trB.scale.y), fabsf(trB.scale.z)});
                    const Vec3& bCenter = aIsSphere ? cB : cA;
                    Vec3 bExt = getScaledBoxExtents(aIsSphere ? idB : idA, aIsSphere ? colB : colA);
                    Vec3 bMin = bCenter - bExt;
                    Vec3 bMax = bCenter + bExt;
                    Vec3 closest(
                        std::clamp(sCenter.x, bMin.x, bMax.x),
                        std::clamp(sCenter.y, bMin.y, bMax.y),
                        std::clamp(sCenter.z, bMin.z, bMax.z)
                    );
                    Vec3 delta = bCenter - closest;
                    Vec3 toSphere = sCenter - closest;
                    float distSq = toSphere.LengthSq();
                    if (distSq <= sRadius * sRadius) {
                        Vec3 pushSphereOut;
                        if (distSq > 1e-8f) {
                            float dist = sqrtf(distSq);
                            pushSphereOut = toSphere * (1.0f / dist);
                            penetration = sRadius - dist;
                        } else {
                            // Sphere center lies inside box; find minimum axis to push out
                            float px = bExt.x - fabsf(sCenter.x - bCenter.x);
                            float py = bExt.y - fabsf(sCenter.y - bCenter.y);
                            float pz = bExt.z - fabsf(sCenter.z - bCenter.z);
                            if (px <= py && px <= pz) {
                                pushSphereOut = Vec3(sCenter.x >= bCenter.x ? 1.0f : -1.0f, 0.0f, 0.0f);
                                penetration = px + sRadius;
                            } else if (py <= pz) {
                                pushSphereOut = Vec3(0.0f, sCenter.y >= bCenter.y ? 1.0f : -1.0f, 0.0f);
                                penetration = py + sRadius;
                            } else {
                                pushSphereOut = Vec3(0.0f, 0.0f, sCenter.z >= bCenter.z ? 1.0f : -1.0f);
                                penetration = pz + sRadius;
                            }
                        }
                        (void)delta;
                        normal = aIsSphere ? (pushSphereOut * -1.0f) : pushSphereOut;
                        hit = true;
                    }
                } else {
                    // BOX / CAPSULE AABB overlap along minimum penetration axis
                    Vec3 eA = getScaledBoxExtents(idA, colA);
                    Vec3 eB = getScaledBoxExtents(idB, colB);
                    Vec3 d = cB - cA;
                    float ox = (eA.x + eB.x) - fabsf(d.x);
                    float oy = (eA.y + eB.y) - fabsf(d.y);
                    float oz = (eA.z + eB.z) - fabsf(d.z);
                    if (ox > 0.0f && oy > 0.0f && oz > 0.0f) {
                        if (ox <= oy && ox <= oz) {
                            normal = Vec3(d.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                            penetration = ox;
                        } else if (oy <= oz) {
                            normal = Vec3(0.0f, d.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
                            penetration = oy;
                        } else {
                            normal = Vec3(0.0f, 0.0f, d.z >= 0.0f ? 1.0f : -1.0f);
                            penetration = oz;
                        }
                        hit = true;
                    }
                }

                if (!hit || penetration <= 0.0f) continue;

                // Positional correction (normal points from A -> B)
                Vec3 correction = normal * (penetration / totalInvMass);
                trA.position = trA.position - correction * invMassA;
                trB.position = trB.position + correction * invMassB;

                Vec3 vA = rbA ? rbA->velocity : Vec3(0, 0, 0);
                Vec3 vB = rbB ? rbB->velocity : Vec3(0, 0, 0);
                Vec3 relVel = vB - vA;
                float velAlongNormal = relVel.Dot(normal);
                if (velAlongNormal < 0.0f) {
                    float rest = std::min(rbA ? rbA->restitution : 0.2f, rbB ? rbB->restitution : 0.2f);
                    float jImp = -(1.0f + rest) * velAlongNormal / totalInvMass;
                    Vec3 impulse = normal * jImp;
                    if (rbA) rbA->velocity = rbA->velocity - impulse * invMassA;
                    if (rbB) rbB->velocity = rbB->velocity + impulse * invMassB;
                }
            }
        }
    }

private:
    EntityID nextEntityId;
};

} // namespace Surreal

