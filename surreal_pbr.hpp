#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - PHYSICALLY BASED RENDERING (PBR) SHADING CORE
 * Modern Microfacet Cook-Torrance Specular & Diffuse Lambertian BRDF
 * High-precision metallic/roughness workflow for bare-metal & hybrid GPUs
 * ============================================================================
 */

#include "surreal_math.hpp"
#include <algorithm>

namespace Surreal {

struct alignas(16) ColorRGB {
    float r, g, b;
    float a;

    constexpr ColorRGB() : r(0), g(0), b(0), a(1.0f) {}
    constexpr ColorRGB(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}

    inline ColorRGB operator+(const ColorRGB& o) const { return ColorRGB(r + o.r, g + o.g, b + o.b, a); }
    inline ColorRGB operator*(const ColorRGB& o) const { return ColorRGB(r * o.r, g * o.g, b * o.b, a); }
    inline ColorRGB operator*(float s) const { return ColorRGB(r * s, g * s, b * s, a); }

    inline uint32_t ToRGBA8() const {
        auto clamp01 = [](float v) -> uint32_t {
            if (v <= 0.0f) return 0;
            if (v >= 1.0f) return 255;
            return static_cast<uint32_t>(v * 255.0f);
        };
        return (clamp01(a) << 24) | (clamp01(r) << 16) | (clamp01(g) << 8) | clamp01(b);
    }
};

struct PBRMaterial {
    ColorRGB albedo;       // Base Color (Linear RGB)
    float roughness;       // Surface micro-roughness [0.04 .. 1.0]
    float metallic;        // Conductor vs dielectric [0.0 .. 1.0]
    float ambientOcclusion;// Cavity AO factor [0.0 .. 1.0]
    ColorRGB emissive;     // Emissive glow radiation

    constexpr PBRMaterial()
        : albedo(ColorRGB(0.8f, 0.8f, 0.8f)),
          roughness(0.5f),
          metallic(0.0f),
          ambientOcclusion(1.0f),
          emissive(ColorRGB(0, 0, 0)) {}
};

struct DirectionalLight {
    Vec3 direction;        // Normalized vector pointing toward light source
    ColorRGB color;        // Radiance intensity in Lux / Lumens
    float intensity;
};

struct PointLight {
    Vec3 position;         // World coordinates
    ColorRGB color;        // Radiant flux
    float intensity;
    float radius;          // Maximum influence attenuation radius
};

/**
 * PBR Shading Math Evaluator (Cook-Torrance)
 */
class PBRShadingCore {
public:
    // 1. Trowbridge-Reitz GGX Normal Distribution Function (NDF)
    static inline float DistributionGGX(const Vec3& N, const Vec3& H, float roughness) {
        float a = roughness * roughness;
        float a2 = a * a;
        float NdotH = std::max(N.Dot(H), 0.0f);
        float NdotH2 = NdotH * NdotH;

        float num = a2;
        float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
        denom = M_PI * denom * denom;

        return num / std::max(denom, 0.0000001f);
    }

    // 2. Geometry Schlick-GGX (Masking-Shadowing)
    static inline float GeometrySchlickGGX(float NdotV, float roughness) {
        float r = (roughness + 1.0f);
        float k = (r * r) / 8.0f;

        float num = NdotV;
        float denom = NdotV * (1.0f - k) + k;

        return num / std::max(denom, 0.0000001f);
    }

    // Smith's method combining light and view shadowing
    static inline float GeometrySmith(const Vec3& N, const Vec3& V, const Vec3& L, float roughness) {
        float NdotV = std::max(N.Dot(V), 0.0f);
        float NdotL = std::max(N.Dot(L), 0.0f);
        float ggx2 = GeometrySchlickGGX(NdotV, roughness);
        float ggx1 = GeometrySchlickGGX(NdotL, roughness);

        return ggx2 * ggx1;
    }

    // 3. Fresnel-Schlick approximation for surface reflection ratio
    static inline Vec3 FresnelSchlick(float cosTheta, const Vec3& F0) {
        float factor = powf(std::max(1.0f - cosTheta, 0.0f), 5.0f);
        return Vec3(
            F0.x + (1.0f - F0.x) * factor,
            F0.y + (1.0f - F0.y) * factor,
            F0.z + (1.0f - F0.z) * factor
        );
    }

    /**
     * Compute Direct Lighting Radiance for a single light source
     */
    static ColorRGB EvaluateDirectLight(
        const Vec3& worldPos,
        const Vec3& normal,
        const Vec3& viewDir,
        const Vec3& lightDir,
        const ColorRGB& lightRadiance,
        const PBRMaterial& mat)
    {
        Vec3 N = normal.Normalized();
        Vec3 V = viewDir.Normalized();
        Vec3 L = lightDir.Normalized();
        Vec3 H = (V + L).Normalized();

        float NdotL = std::max(N.Dot(L), 0.0f);
        if (NdotL <= 0.0f) return ColorRGB(0, 0, 0);

        float NdotV = std::max(N.Dot(V), 0.0f);

        // Dielectric base reflectivity: 0.04 (plastic/water) up to albedo for metals
        Vec3 baseF0(0.04f, 0.04f, 0.04f);
        Vec3 albedoV(mat.albedo.r, mat.albedo.g, mat.albedo.b);
        Vec3 F0(
            baseF0.x * (1.0f - mat.metallic) + albedoV.x * mat.metallic,
            baseF0.y * (1.0f - mat.metallic) + albedoV.y * mat.metallic,
            baseF0.z * (1.0f - mat.metallic) + albedoV.z * mat.metallic
        );

        // Cook-Torrance Specular BRDF terms
        float D = DistributionGGX(N, H, mat.roughness);
        float G = GeometrySmith(N, V, L, mat.roughness);
        Vec3 F = FresnelSchlick(std::max(H.Dot(V), 0.0f), F0);

        Vec3 numerator = F * (D * G);
        float denominator = 4.0f * std::max(NdotV, 0.001f) * std::max(NdotL, 0.001f);
        Vec3 specular = numerator / std::max(denominator, 0.001f);

        // Energy conservation: kS is specular fraction, kD is diffuse
        Vec3 kS = F;
        Vec3 kD = Vec3(1.0f - kS.x, 1.0f - kS.y, 1.0f - kS.z) * (1.0f - mat.metallic);

        // Lambertian diffuse
        Vec3 diffuse = Vec3(
            (kD.x * albedoV.x) / M_PI,
            (kD.y * albedoV.y) / M_PI,
            (kD.z * albedoV.z) / M_PI
        );

        // Outgoing direct radiance Lo
        float outR = (diffuse.x + specular.x) * lightRadiance.r * NdotL;
        float outG = (diffuse.y + specular.y) * lightRadiance.g * NdotL;
        float outB = (diffuse.z + specular.z) * lightRadiance.b * NdotL;

        return ColorRGB(outR, outG, outB);
    }
};

} // namespace Surreal

