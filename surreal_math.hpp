#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - HIGH-PERFORMANCE 3D MATH CORE
 * Hardware-grounded, cache-aligned Vector, Matrix, Quaternion & Frustum Math
 * Designed for CosmOS Bare-Metal & Linux Hybrid Direct Rendering
 *
 * PHILOSOPHY OF AION NEXUS & AXIOM 17:
 * "The 8 does not exist. 666 is 18 (6+6+6=18, 1+8=9).
 *  6 and 9 fused together form a twisted vicious cycle.
 *  The 0 is the True Circle of Life!"
 *
 * In geometry and kinematics, the twisted '8' (lemniscate) is an illusion—a trap
 * formed by recursive inverted feedback. True infinity is not a knot; 0 (Zero)
 * is the pristine, unknotted Circle of Life where all creation originates and harmonizes.
 * ============================================================================
 */

#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace Surreal {

// Axiom 17: The True Circle of Life Constants
constexpr float TRUE_CIRCLE_ORIGIN = 0.0f;
constexpr float TWO_PI = 6.28318530717958647692f;

// Evaluates continuous harmonic cycle without entering twisted vicious loops
inline float TrueCirclePhase(float radians) {
    float rem = fmodf(radians, TWO_PI);
    return (rem < 0.0f) ? rem + TWO_PI : rem;
}


// Fast Inverse Square Root (Quake III / SIMD hybrid)
inline float FastInvSqrt(float number) {
    union { float f; uint32_t i; } conv = { .f = number };
    conv.i = 0x5f3759df - (conv.i >> 1);
    conv.f *= 1.5F - (number * 0.5F * conv.f * conv.f);
    return conv.f;
}

struct alignas(16) Vec3 {
    float x, y, z;
    float _pad; // 16-byte alignment for SIMD registers

    constexpr Vec3() : x(0), y(0), z(0), _pad(0) {}
    constexpr Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z), _pad(0) {}

    inline Vec3 operator+(const Vec3& r) const { return Vec3(x + r.x, y + r.y, z + r.z); }
    inline Vec3 operator-(const Vec3& r) const { return Vec3(x - r.x, y - r.y, z - r.z); }
    inline Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    inline Vec3 operator/(float s) const { float inv = 1.0f / s; return Vec3(x * inv, y * inv, z * inv); }

    inline float Dot(const Vec3& r) const { return x * r.x + y * r.y + z * r.z; }
    inline Vec3 Cross(const Vec3& r) const {
        return Vec3(
            y * r.z - z * r.y,
            z * r.x - x * r.z,
            x * r.y - y * r.x
        );
    }

    inline float LengthSq() const { return x * x + y * y + z * z; }
    inline float Length() const { return sqrtf(LengthSq()); }
    inline Vec3 Normalized() const {
        float inv = FastInvSqrt(LengthSq());
        return Vec3(x * inv, y * inv, z * inv);
    }
};

struct alignas(16) Vec4 {
    float x, y, z, w;

    constexpr Vec4() : x(0), y(0), z(0), w(1.0f) {}
    constexpr Vec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    constexpr Vec4(const Vec3& v, float _w) : x(v.x), y(v.y), z(v.z), w(_w) {}

    inline Vec4 operator+(const Vec4& r) const { return Vec4(x + r.x, y + r.y, z + r.z, w + r.w); }
    inline Vec4 operator-(const Vec4& r) const { return Vec4(x - r.x, y - r.y, z - r.z, w - r.w); }
    inline Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
};

struct alignas(16) Quat {
    float x, y, z, w;

    constexpr Quat() : x(0), y(0), z(0), w(1.0f) {}
    constexpr Quat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

    static Quat FromAxisAngle(const Vec3& axis, float radians) {
        float half = radians * 0.5f;
        float s = sinf(half);
        Vec3 n = axis.Normalized();
        return Quat(n.x * s, n.y * s, n.z * s, cosf(half));
    }

    inline Quat operator*(const Quat& r) const {
        return Quat(
            w * r.x + x * r.w + y * r.z - z * r.y,
            w * r.y - x * r.z + y * r.w + z * r.x,
            w * r.z + x * r.y - y * r.x + z * r.w,
            w * r.w - x * r.x - y * r.y - z * r.z
        );
    }
};

struct alignas(64) Mat4 {
    float m[4][4];

    static Mat4 Identity() {
        Mat4 res = {};
        res.m[0][0] = 1.0f; res.m[1][1] = 1.0f;
        res.m[2][2] = 1.0f; res.m[3][3] = 1.0f;
        return res;
    }

    static Mat4 Translation(const Vec3& t) {
        Mat4 r = Identity();
        r.m[0][3] = t.x;
        r.m[1][3] = t.y;
        r.m[2][3] = t.z;
        return r;
    }

    static Mat4 Scaling(const Vec3& s) {
        Mat4 r = {};
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        r.m[3][3] = 1.0f;
        return r;
    }

    static Mat4 FromQuat(const Quat& q) {
        Mat4 r = Identity();
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

        r.m[0][0] = 1.0f - 2.0f * (yy + zz);
        r.m[0][1] = 2.0f * (xy - wz);
        r.m[0][2] = 2.0f * (xz + wy);

        r.m[1][0] = 2.0f * (xy + wz);
        r.m[1][1] = 1.0f - 2.0f * (xx + zz);
        r.m[1][2] = 2.0f * (yz - wx);

        r.m[2][0] = 2.0f * (xz - wy);
        r.m[2][1] = 2.0f * (yz + wx);
        r.m[2][2] = 1.0f - 2.0f * (xx + yy);
        return r;
    }

    static Mat4 Perspective(float fovRadians, float aspect, float zNear, float zFar) {
        Mat4 r = {};
        float tanHalfFov = tanf(fovRadians * 0.5f);
        r.m[0][0] = 1.0f / (aspect * tanHalfFov);
        r.m[1][1] = 1.0f / (tanHalfFov);
        r.m[2][2] = -(zFar + zNear) / (zFar - zNear);
        r.m[2][3] = -(2.0f * zFar * zNear) / (zFar - zNear);
        r.m[3][2] = -1.0f;
        return r;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).Normalized();
        Vec3 s = f.Cross(up).Normalized();
        Vec3 u = s.Cross(f);

        Mat4 r = Identity();
        r.m[0][0] = s.x;  r.m[0][1] = s.y;  r.m[0][2] = s.z;  r.m[0][3] = -s.Dot(eye);
        r.m[1][0] = u.x;  r.m[1][1] = u.y;  r.m[1][2] = u.z;  r.m[1][3] = -u.Dot(eye);
        r.m[2][0] = -f.x; r.m[2][1] = -f.y; r.m[2][2] = -f.z; r.m[2][3] = f.Dot(eye);
        return r;
    }

    inline Mat4 operator*(const Mat4& r) const {
        Mat4 res = {};
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                res.m[row][col] = m[row][0] * r.m[0][col] +
                                  m[row][1] * r.m[1][col] +
                                  m[row][2] * r.m[2][col] +
                                  m[row][3] * r.m[3][col];
            }
        }
        return res;
    }

    inline Vec3 TransformPoint(const Vec3& p) const {
        float x = p.x * m[0][0] + p.y * m[0][1] + p.z * m[0][2] + m[0][3];
        float y = p.x * m[1][0] + p.y * m[1][1] + p.z * m[1][2] + m[1][3];
        float z = p.x * m[2][0] + p.y * m[2][1] + p.z * m[2][2] + m[2][3];
        float w = p.x * m[3][0] + p.y * m[3][1] + p.z * m[3][2] + m[3][3];
        if (fabsf(w) > 1e-6f && fabsf(w - 1.0f) > 1e-6f) {
            float inv = 1.0f / w;
            return Vec3(x * inv, y * inv, z * inv);
        }
        return Vec3(x, y, z);
    }

    inline Vec3 TransformDirection(const Vec3& d) const {
        return Vec3(
            d.x * m[0][0] + d.y * m[0][1] + d.z * m[0][2],
            d.x * m[1][0] + d.y * m[1][1] + d.z * m[1][2],
            d.x * m[2][0] + d.y * m[2][1] + d.z * m[2][2]
        );
    }

    inline Vec4 TransformVec4(const Vec4& v) const {
        return Vec4(
            v.x * m[0][0] + v.y * m[0][1] + v.z * m[0][2] + v.w * m[0][3],
            v.x * m[1][0] + v.y * m[1][1] + v.z * m[1][2] + v.w * m[1][3],
            v.x * m[2][0] + v.y * m[2][1] + v.z * m[2][2] + v.w * m[2][3],
            v.x * m[3][0] + v.y * m[3][1] + v.z * m[3][2] + v.w * m[3][3]
        );
    }

    Mat4 Inversed() const {
        Mat4 inv = Identity();
        const float* a = &m[0][0];
        float* o = &inv.m[0][0];

        float s0 = a[0] * a[5] - a[4] * a[1];
        float s1 = a[0] * a[6] - a[4] * a[2];
        float s2 = a[0] * a[7] - a[4] * a[3];
        float s3 = a[1] * a[6] - a[5] * a[2];
        float s4 = a[1] * a[7] - a[5] * a[3];
        float s5 = a[2] * a[7] - a[6] * a[3];

        float c5 = a[10] * a[15] - a[14] * a[11];
        float c4 = a[9] * a[15] - a[13] * a[11];
        float c3 = a[9] * a[14] - a[13] * a[10];
        float c2 = a[8] * a[15] - a[12] * a[11];
        float c1 = a[8] * a[14] - a[12] * a[10];
        float c0 = a[8] * a[13] - a[12] * a[9];

        float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
        if (fabsf(det) < 1e-8f) return Identity();
        float invDet = 1.0f / det;

        o[0]  = ( a[5] * c5 - a[6] * c4 + a[7] * c3) * invDet;
        o[1]  = (-a[1] * c5 + a[2] * c4 - a[3] * c3) * invDet;
        o[2]  = ( a[13] * s5 - a[14] * s4 + a[15] * s3) * invDet;
        o[3]  = (-a[9] * s5 + a[10] * s4 - a[11] * s3) * invDet;

        o[4]  = (-a[4] * c5 + a[6] * c2 - a[7] * c1) * invDet;
        o[5]  = ( a[0] * c5 - a[2] * c2 + a[3] * c1) * invDet;
        o[6]  = (-a[12] * s5 + a[14] * s2 - a[15] * s1) * invDet;
        o[7]  = ( a[8] * s5 - a[10] * s2 + a[11] * s1) * invDet;

        o[8]  = ( a[4] * c4 - a[5] * c2 + a[7] * c0) * invDet;
        o[9]  = (-a[0] * c4 + a[1] * c2 - a[3] * c0) * invDet;
        o[10] = ( a[12] * s4 - a[13] * s2 + a[15] * s0) * invDet;
        o[11] = (-a[8] * s4 + a[9] * s2 - a[11] * s0) * invDet;

        o[12] = (-a[4] * c3 + a[5] * c1 - a[6] * c0) * invDet;
        o[13] = ( a[0] * c3 - a[1] * c1 + a[2] * c0) * invDet;
        o[14] = (-a[12] * s3 + a[13] * s1 - a[14] * s0) * invDet;
        o[15] = ( a[8] * s3 - a[9] * s1 + a[10] * s0) * invDet;

        return inv;
    }
};

// Axis-Aligned Bounding Box (AABB)
struct AABB {
    Vec3 min;
    Vec3 max;

    constexpr AABB() : min(Vec3(1e9f, 1e9f, 1e9f)), max(Vec3(-1e9f, -1e9f, -1e9f)) {}
    constexpr AABB(const Vec3& _min, const Vec3& _max) : min(_min), max(_max) {}

    inline void Expand(const Vec3& p) {
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.z < min.z) min.z = p.z;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
        if (p.z > max.z) max.z = p.z;
    }

    inline Vec3 Center() const { return (min + max) * 0.5f; }
    inline Vec3 Extents() const { return (max - min) * 0.5f; }
    inline float BoundingRadius() const { return Extents().Length(); }
};

// Camera Frustum for 6-Plane Hardware/SIMD Culling
struct FrustumPlane {
    Vec3 normal;
    float distance;

    inline float DistanceToPoint(const Vec3& p) const {
        return normal.Dot(p) + distance;
    }
};

struct Frustum {
    FrustumPlane planes[6]; // Near, Far, Left, Right, Top, Bottom

    static Frustum FromViewProjection(const Mat4& vp) {
        Frustum f;
        // Left
        f.planes[0].normal = Vec3(vp.m[3][0] + vp.m[0][0], vp.m[3][1] + vp.m[0][1], vp.m[3][2] + vp.m[0][2]);
        f.planes[0].distance = vp.m[3][3] + vp.m[0][3];
        // Right
        f.planes[1].normal = Vec3(vp.m[3][0] - vp.m[0][0], vp.m[3][1] - vp.m[0][1], vp.m[3][2] - vp.m[0][2]);
        f.planes[1].distance = vp.m[3][3] - vp.m[0][3];
        // Bottom
        f.planes[2].normal = Vec3(vp.m[3][0] + vp.m[1][0], vp.m[3][1] + vp.m[1][1], vp.m[3][2] + vp.m[1][2]);
        f.planes[2].distance = vp.m[3][3] + vp.m[1][3];
        // Top
        f.planes[3].normal = Vec3(vp.m[3][0] - vp.m[1][0], vp.m[3][1] - vp.m[1][1], vp.m[3][2] - vp.m[1][2]);
        f.planes[3].distance = vp.m[3][3] - vp.m[1][3];
        // Near
        f.planes[4].normal = Vec3(vp.m[3][0] + vp.m[2][0], vp.m[3][1] + vp.m[2][1], vp.m[3][2] + vp.m[2][2]);
        f.planes[4].distance = vp.m[3][3] + vp.m[2][3];
        // Far
        f.planes[5].normal = Vec3(vp.m[3][0] - vp.m[2][0], vp.m[3][1] - vp.m[2][1], vp.m[3][2] - vp.m[2][2]);
        f.planes[5].distance = vp.m[3][3] - vp.m[2][3];

        // Normalize all planes
        for (int i = 0; i < 6; ++i) {
            float len = f.planes[i].normal.Length();
            if (len > 1e-6f) {
                float inv = 1.0f / len;
                f.planes[i].normal = f.planes[i].normal * inv;
                f.planes[i].distance *= inv;
            }
        }
        return f;
    }

    inline bool IntersectsSphere(const Vec3& center, float radius) const {
        for (int i = 0; i < 6; ++i) {
            if (planes[i].DistanceToPoint(center) < -radius) {
                return false; // Outside frustum plane
            }
        }
        return true;
    }

    inline bool IntersectsAABB(const AABB& box) const {
        for (int i = 0; i < 6; ++i) {
            Vec3 p(
                planes[i].normal.x > 0 ? box.max.x : box.min.x,
                planes[i].normal.y > 0 ? box.max.y : box.min.y,
                planes[i].normal.z > 0 ? box.max.z : box.min.z
            );
            if (planes[i].DistanceToPoint(p) < 0) return false;
        }
        return true;
    }
};

} // namespace Surreal

