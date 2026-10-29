#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - INTERACTIVE 3D CAMERA CONTROLLER
 * Supports Orbital Third-Person (Spring-Arm), Free-Flight (WASD), and First-Person
 * Seamless Hybrid Input for Linux & Bare-Metal CosmOS
 * ============================================================================
 */

#include "surreal_math.hpp"
#include <algorithm>

namespace Surreal {

enum class CameraMode {
    ORBIT_THIRD_PERSON,
    FREE_FLIGHT_WASD,
    FIRST_PERSON
};

struct CameraInputState {
    bool moveForward;
    bool moveBackward;
    bool moveLeft;
    bool moveRight;
    bool moveUp;
    bool moveDown;

    float mouseDeltaX;
    float mouseDeltaY;
    float scrollDelta; // Zoom in / out

    bool isRightMouseDown;
    bool isLeftMouseDown;

    CameraInputState()
        : moveForward(false), moveBackward(false),
          moveLeft(false), moveRight(false),
          moveUp(false), moveDown(false),
          mouseDeltaX(0), mouseDeltaY(0), scrollDelta(0),
          isRightMouseDown(false), isLeftMouseDown(false) {}
};

class InteractiveCameraController {
public:
    CameraMode mode;

    // Orbital parameters
    Vec3 targetPosition;      // Point the orbital camera revolves around (e.g. character head/center)
    float distance;           // Distance from target
    float targetDistance;     // For smooth zoom spring-arm
    float yaw;                // Azimuth angle in radians
    float pitch;              // Elevation angle in radians
    float minPitch;
    float maxPitch;
    float minDistance;
    float maxDistance;
    float minFloorY;

    // Free flight parameters
    Vec3 freeCameraPos;
    float moveSpeed;
    float mouseSensitivity;
    float smoothingFactor;

    InteractiveCameraController()
        : mode(CameraMode::ORBIT_THIRD_PERSON),
          targetPosition(0, 1.25f, 0),
          distance(2.8f),
          targetDistance(2.8f),
          yaw(0.0f),
          pitch(0.28f),          // ~ 16 degrees down-angle, classic AAA action view
          minPitch(-0.05f),      // Blocked from dipping below ground level
          maxPitch(1.35f),       // Up to nearly top-down view (~77 degrees)
          minDistance(1.2f),     // Cannot zoom inside hero body
          maxDistance(6.0f),     // Clamped so hero never shrinks into distance
          minFloorY(0.35f),      // Hard floor boundary: Camera cannot descend below floor
          freeCameraPos(0, 2.0f, 5.0f),
          moveSpeed(6.0f),
          mouseSensitivity(0.0030f),
          smoothingFactor(14.0f) {}

    void SetTarget(const Vec3& target) {
        targetPosition = target;
    }

    void Update(float dt, const CameraInputState& input) {
        if (mode == CameraMode::ORBIT_THIRD_PERSON) {
            UpdateOrbit(dt, input);
        } else if (mode == CameraMode::FREE_FLIGHT_WASD) {
            UpdateFreeFlight(dt, input);
        }
    }

    Vec3 GetPosition() const {
        if (mode == CameraMode::ORBIT_THIRD_PERSON) {
            // Spherical to Cartesian
            float cosPitch = cosf(pitch);
            float sinPitch = sinf(pitch);
            float cosYaw = cosf(yaw);
            float sinYaw = sinf(yaw);

            float cx = targetPosition.x + distance * cosPitch * sinYaw;
            float cy = targetPosition.y + distance * sinPitch;
            float cz = targetPosition.z + distance * cosPitch * cosYaw;

            // Strict floor boundary: Camera cannot go below minFloorY
            if (cy < minFloorY) {
                cy = minFloorY;
            }

            return Vec3(cx, cy, cz);
        }
        return freeCameraPos;
    }

    Vec3 GetTarget() const {
        if (mode == CameraMode::ORBIT_THIRD_PERSON) {
            return targetPosition;
        }
        // Free flight forward vector
        float cosPitch = cosf(pitch);
        float sinPitch = sinf(pitch);
        float cosYaw = cosf(yaw);
        float sinYaw = sinf(yaw);

        Vec3 forward(
            cosPitch * sinYaw,
            sinPitch,
            cosPitch * cosYaw
        );
        return freeCameraPos + forward;
    }

    Vec3 GetUp() const {
        return Vec3(0, 1.0f, 0);
    }

    Mat4 GetViewMatrix() const {
        return Mat4::LookAt(GetPosition(), GetTarget(), GetUp());
    }

private:
    void UpdateOrbit(float dt, const CameraInputState& input) {
        // Dynamic pitch clamping: calculate lowest pitch that keeps camera strictly above minFloorY
        float effMinPitch = minPitch;
        float heightAboveFloor = targetPosition.y - minFloorY;
        if (distance > 0.05f) {
            float ratio = -heightAboveFloor / distance;
            if (ratio >= -1.0f && ratio <= 1.0f) {
                float floorPitchLimit = asinf(ratio) + 0.02f; // Safety buffer above floor
                effMinPitch = std::max(minPitch, floorPitchLimit);
            } else if (ratio > 1.0f) {
                effMinPitch = 0.0f;
            }
        }

        // Orbit rotation by mouse delta (when dragging or free looking)
        if (input.isLeftMouseDown || input.isRightMouseDown || fabsf(input.mouseDeltaX) > 0 || fabsf(input.mouseDeltaY) > 0) {
            yaw += input.mouseDeltaX * mouseSensitivity;
            pitch += input.mouseDeltaY * mouseSensitivity;
            pitch = std::max(effMinPitch, std::min(maxPitch, pitch));
        } else {
            pitch = std::max(effMinPitch, std::min(maxPitch, pitch));
        }

        // Zoom Spring-Arm (gentle step clamped strictly)
        if (fabsf(input.scrollDelta) > 0) {
            targetDistance -= input.scrollDelta * 0.35f;
            targetDistance = std::max(minDistance, std::min(maxDistance, targetDistance));
        }

        // Exponential smoothing for buttery cinematic feel
        float alpha = std::min(1.0f, dt * smoothingFactor);
        distance += (targetDistance - distance) * alpha;

        // Re-check pitch against smoothed distance
        pitch = std::max(effMinPitch, std::min(maxPitch, pitch));

        // WASD to pan the orbital target in world space
        Vec3 forward = Vec3(-sinf(yaw), 0, -cosf(yaw)).Normalized();
        Vec3 right = forward.Cross(Vec3(0, 1.0f, 0)).Normalized();

        float panSpeed = moveSpeed * dt;
        if (input.moveForward) targetPosition = targetPosition + (forward * panSpeed);
        if (input.moveBackward) targetPosition = targetPosition - (forward * panSpeed);
        if (input.moveRight) targetPosition = targetPosition + (right * panSpeed);
        if (input.moveLeft) targetPosition = targetPosition - (right * panSpeed);
        if (input.moveUp) targetPosition.y += panSpeed;
        if (input.moveDown) targetPosition.y -= panSpeed;
    }

    void UpdateFreeFlight(float dt, const CameraInputState& input) {
        yaw += input.mouseDeltaX * mouseSensitivity;
        pitch += input.mouseDeltaY * mouseSensitivity;
        pitch = std::max(minPitch, std::min(maxPitch, pitch));

        float cosPitch = cosf(pitch);
        float sinPitch = sinf(pitch);
        float cosYaw = cosf(yaw);
        float sinYaw = sinf(yaw);

        Vec3 forward(cosPitch * sinYaw, sinPitch, cosPitch * cosYaw);
        Vec3 right = forward.Cross(Vec3(0, 1.0f, 0)).Normalized();
        Vec3 up(0, 1.0f, 0);

        float speed = moveSpeed * dt;
        if (input.moveForward) freeCameraPos = freeCameraPos + (forward * speed);
        if (input.moveBackward) freeCameraPos = freeCameraPos - (forward * speed);
        if (input.moveRight) freeCameraPos = freeCameraPos + (right * speed);
        if (input.moveLeft) freeCameraPos = freeCameraPos - (right * speed);
        if (input.moveUp) freeCameraPos = freeCameraPos + (up * speed);
        if (input.moveDown) freeCameraPos = freeCameraPos - (up * speed);
    }
};

} // namespace Surreal

