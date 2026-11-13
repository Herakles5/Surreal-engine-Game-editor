#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - CPU + GPU HYBRID DUAL-BACKEND RENDERER
 * Unites Bare-Metal AVX2 SIMD Software Rasterizer with Hardware OpenGL 4.6 PBR
 * Seamless Live Runtime Switching via [F9] with Zero Frame Drops
 * ============================================================================
 */

#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include "surreal_meshlet.hpp"
#include "surreal_ecs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Surreal {

enum class RenderBackend {
    CPU_AVX2,     // Bare-Metal CosmOS Software SIMD Rasterizer
    GPU_OPENGL    // Linux Desktop OpenGL 4.6 Hardware Core Profile
};

struct GLMeshBuffer {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint skinVbo = 0;
    size_t vertexCount = 0;
    size_t indexCount = 0;
    size_t skinVertexCount = 0;
    bool hasBindPoseVBO = false;
};

struct GPUSkinVertex {
    uint32_t joints[4];
    float weights[4];
};

class SurrealHybridRenderer {
public:
    static constexpr int MAX_GPU_BONES = 250;
    static constexpr int NUM_SHADOW_CASCADES = 3;
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;

    RenderBackend backend;
    SDL_Window* window;
    SDL_GLContext glContext;
    bool isGLInitialized;

    uint32_t screenWidth;
    uint32_t screenHeight;

    // OpenGL Shaders & Resources
    GLuint pbrProg;
    GLuint quadProg;
    GLuint skyProg;
    GLuint shadowProg;
    GLuint bloomExtractProg = 0;
    GLuint bloomBlurProg = 0;
    GLuint hdrCompositeProg = 0;
    GLuint quadVAO, quadVBO;
    GLuint screenTex;
    GLuint hudTex;

    // GPU Bone Palette UBO (binding point 0)
    GLuint boneUBO = 0;

    // 3-Cascade Directional Shadow Map FBO & Texture2DArray (2048x2048x3)
    GLuint shadowFBO;
    GLuint shadowDepthTex;
    Mat4 lightSpaceMatrix;
    Mat4 cascadeMatrices[NUM_SHADOW_CASCADES];
    float cascadeSplits[NUM_SHADOW_CASCADES] = {16.0f, 55.0f, 160.0f};
    int activeShadowCascade = 0;
    int renderedShadowCascades = 0;
    bool shadowMapReady = false;

    // HDR 16-bit Floating-Point Scene FBO & Separable Bloom Ping-Pong Buffers
    GLuint hdrFBO = 0;
    GLuint hdrColorTex = 0;
    GLuint hdrDepthRBO = 0;
    GLuint bloomFBO[2] = {0, 0};
    GLuint bloomTex[2] = {0, 0};
    uint32_t bloomWidth = 0;
    uint32_t bloomHeight = 0;
    bool hdrEnabled = true;
    bool hdrPipelineReady = false;
    bool hdrSceneOpen = false;
    float bloomThreshold = 1.05f;
    float bloomIntensity = 0.32f;
    float exposure = 1.0f;

    // Uniform Locations
    GLint uMVPLoc;
    GLint uModelLoc;
    GLint uNormalMatLoc;
    GLint uLightSpaceMatrixLoc;
    GLint uCascadeSplitsLoc = -1;
    GLint uNumShadowCascadesLoc = -1;
    GLint uShadowMapLoc;
    GLint uShadowMapEnabledLoc;
    GLint uBeatPulseLoc;
    GLint uShadowMVPLoc;
    GLint uShadowUseSkinningLoc = -1;
    GLint uShadowFootOffsetLoc = -1;
    GLint uSunDirLoc;
    GLint uSunColorLoc;
    GLint uAmbientColorLoc;
    GLint uCamPosLoc;
    GLint uAlbedoLoc;
    GLint uMetallicLoc;
    GLint uRoughnessLoc;
    GLint uAmbientOcclusionLoc = -1;
    GLint uEmissiveLoc;
    GLint uTimeLoc;
    GLint uNumPointLightsLoc;
    GLint uPointLightPosLoc;
    GLint uPointLightColLoc;
    GLint uPointLightRadiusLoc;
    GLint uAlbedoMapLoc;
    GLint uNormalMapLoc = -1;
    GLint uMRMapLoc = -1;
    GLint uAOMapLoc = -1;
    GLint uEmissiveMapLoc = -1;
    GLint uHasTextureLoc;
    GLint uHasNormalMapLoc = -1;
    GLint uHasMRMapLoc = -1;
    GLint uHasAOMapLoc = -1;
    GLint uHasEmissiveMapLoc = -1;
    GLint uAlphaModeLoc = -1;
    GLint uAlphaCutoffLoc = -1;
    GLint uAllowWetPuddlesLoc = -1;
    GLint uUseSkinningLoc = -1;
    GLint uFootOffsetLoc = -1;
    GLint uOutputLinearHDRLoc = -1;
    GLint uFogColorLoc;
    GLint uWetnessLoc;
    GLint uGlowIntensityLoc;
    GLint uShaderQualityLoc;

    float wetness = 0.85f;
    float glowIntensity = 1.30f;
    float beatPulse = 0.0f;
    float activeBPM = 128.0f; // Synchronized live with Dojo Choreo-Memory-Link BPM
    int shaderQuality = 1; // 0 = Fast (30-60+ FPS), 1 = Balanced, 2 = Ultra

    // Sky Dome Uniform Locations
    GLint uSkyInvVPLoc;
    GLint uSkyZenithLoc;
    GLint uSkyHorizonLoc;
    GLint uSkyGroundLoc;
    GLint uSkySunDirLoc;
    GLint uSkySunColLoc;
    GLint uSkyIsNightLoc;
    GLint uSkyOutputLinearHDRLoc = -1;

    // Bloom & HDR Composite Uniform Locations
    GLint uBloomThresholdLoc = -1;
    GLint uBloomBlurHorizLoc = -1;
    GLint uCompositeHdrLoc = -1;
    GLint uCompositeBloomLoc = -1;
    GLint uCompositeBloomIntLoc = -1;
    GLint uCompositeExposureLoc = -1;

    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;

    std::unordered_map<const SurrealMesh*, GLMeshBuffer> meshBuffers;
    std::unordered_set<GLuint> ownedTextures;

    SurrealHybridRenderer()
        : backend(RenderBackend::GPU_OPENGL),
          window(nullptr),
          glContext(nullptr),
          isGLInitialized(false),
          screenWidth(960),
          screenHeight(640),
          pbrProg(0), quadProg(0), skyProg(0), shadowProg(0),
          quadVAO(0), quadVBO(0),
          screenTex(0), hudTex(0),
          shadowFBO(0), shadowDepthTex(0),
          lightSpaceMatrix(Mat4::Identity()),
          uMVPLoc(-1), uModelLoc(-1), uNormalMatLoc(-1),
          uLightSpaceMatrixLoc(-1), uShadowMapLoc(-1),
          uShadowMapEnabledLoc(-1), uBeatPulseLoc(-1),
          uShadowMVPLoc(-1),
          uSunDirLoc(-1), uSunColorLoc(-1),
          uAmbientColorLoc(-1), uCamPosLoc(-1),
          uAlbedoLoc(-1), uMetallicLoc(-1),
          uRoughnessLoc(-1), uEmissiveLoc(-1),
          uTimeLoc(-1), uNumPointLightsLoc(-1),
          uPointLightPosLoc(-1), uPointLightColLoc(-1),
          uPointLightRadiusLoc(-1), uAlbedoMapLoc(-1),
          uHasTextureLoc(-1), uFogColorLoc(-1),
          uWetnessLoc(-1), uGlowIntensityLoc(-1), uShaderQualityLoc(-1),
          uSkyInvVPLoc(-1), uSkyZenithLoc(-1),
          uSkyHorizonLoc(-1), uSkyGroundLoc(-1),
          uSkySunDirLoc(-1), uSkySunColLoc(-1),
          uSkyIsNightLoc(-1) {}

    ~SurrealHybridRenderer() {
        Cleanup();
    }

    bool Init(SDL_Window* win, uint32_t w, uint32_t h) {
        window = win;
        screenWidth = w;
        screenHeight = h;

        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            std::cerr << "[-] Failed to create SDL_GLContext: " << SDL_GetError() << "\n";
            backend = RenderBackend::CPU_AVX2;
            return false;
        }

        SDL_GL_MakeCurrent(window, glContext);
        SDL_GL_SetSwapInterval(0); // Unlocked framerate (prevents 60->30->15 FPS VSync stall)

        glVendor = (const char*)glGetString(GL_VENDOR);
        glRenderer = (const char*)glGetString(GL_RENDERER);
        glVersion = (const char*)glGetString(GL_VERSION);

        std::cout << "[✓] OpenGL Hybrid Backend Initialized:\n";
        std::cout << "    Vendor:   " << glVendor << "\n";
        std::cout << "    Renderer: " << glRenderer << "\n";
        std::cout << "    Version:  " << glVersion << "\n";

        if (!BuildShaders()) {
            std::cerr << "[-] Failed to compile OpenGL shaders! Reverting to CPU AVX2.\n";
            backend = RenderBackend::CPU_AVX2;
            return false;
        }

        SetupFullscreenQuad();
        SetupTextures();
        SetupBoneUBO();
        SetupShadowMap();
        SetupHDRPipeline();

        isGLInitialized = true;
        return true;
    }

    /**
     * Compute 3 texel-snapped directional shadow cascades centered around focusPos.
     */
    void ComputeCascadeMatrices(const Vec3& sunDir, const Vec3& focusPos, const Vec3& viewForward = Vec3(0, 0, -1)) {
        Vec3 lightDir = sunDir.Normalized();
        if (lightDir.LengthSq() <= 1e-12f) lightDir = Vec3(0.577f, 0.577f, 0.577f);
        if (lightDir.y < 0.14f) lightDir.y = 0.14f;
        lightDir = lightDir.Normalized();

        Vec3 fwdXZ(viewForward.x, 0.0f, viewForward.z);
        if (fwdXZ.LengthSq() > 1e-6f) fwdXZ = fwdXZ.Normalized();
        else fwdXZ = Vec3(0.0f, 0.0f, -1.0f);

        const float radii[NUM_SHADOW_CASCADES] = {15.0f, 48.0f, 135.0f};
        const float fwdOffsets[NUM_SHADOW_CASCADES] = {4.0f, 14.0f, 36.0f};
        const float lightDist[NUM_SHADOW_CASCADES] = {38.0f, 75.0f, 140.0f};
        const float zFar[NUM_SHADOW_CASCADES] = {96.0f, 180.0f, 320.0f};

        for (int c = 0; c < NUM_SHADOW_CASCADES; ++c) {
            float r = radii[c];
            Vec3 center = focusPos + fwdXZ * fwdOffsets[c];

            Mat4 lightView = Mat4::LookAt(center + lightDir * lightDist[c], center, Vec3(0.0f, 1.0f, 0.0f));
            // Texel-snap light space center to stabilize shadow edges while moving
            Vec3 centerLS = lightView.TransformPoint(center);
            float worldUnitsPerTexel = (2.0f * r) / static_cast<float>(SHADOW_MAP_SIZE);
            centerLS.x = floorf(centerLS.x / worldUnitsPerTexel) * worldUnitsPerTexel;
            centerLS.y = floorf(centerLS.y / worldUnitsPerTexel) * worldUnitsPerTexel;

            Mat4 lightProj = Mat4::Orthographic(
                centerLS.x - r, centerLS.x + r,
                centerLS.y - r, centerLS.y + r,
                1.0f, zFar[c]
            );
            cascadeMatrices[c] = lightProj * lightView;
        }
        lightSpaceMatrix = cascadeMatrices[0];
    }

    bool BeginShadowCascade(int cascadeIdx) {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || shadowFBO == 0 || shadowProg == 0 || shadowDepthTex == 0) {
            return false;
        }
        if (cascadeIdx < 0 || cascadeIdx >= NUM_SHADOW_CASCADES) return false;
        activeShadowCascade = cascadeIdx;
        renderedShadowCascades = std::max(renderedShadowCascades, cascadeIdx + 1);
        lightSpaceMatrix = cascadeMatrices[cascadeIdx];

        glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowDepthTex, 0, cascadeIdx);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.8f, 3.0f);
        glUseProgram(shadowProg);
        if (uShadowUseSkinningLoc != -1) glUniform1i(uShadowUseSkinningLoc, 0);
        if (uShadowFootOffsetLoc != -1) glUniform1f(uShadowFootOffsetLoc, 0.0f);
        return true;
    }

    /**
     * Begin Directional Shadow Depth Pass (2048x2048x3 CSM centered around focusPos)
     */
    void BeginShadowPass(const Vec3& sunDir, const Vec3& focusPos, const Vec3& viewForward = Vec3(0, 0, -1)) {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || shadowFBO == 0 || shadowProg == 0) return;
        renderedShadowCascades = 0;
        ComputeCascadeMatrices(sunDir, focusPos, viewForward);
        BeginShadowCascade(0);
    }

    void RenderMeshShadow(
        const std::shared_ptr<SurrealMesh>& mesh,
        const Mat4& worldMat,
        bool isDynamic = false,
        bool uploadDynamicVBO = false)
    {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || !mesh || shadowFBO == 0 || shadowProg == 0) return;

        EnsureMeshUploaded(mesh, isDynamic, uploadDynamicVBO);
        auto it = meshBuffers.find(mesh.get());
        if (it == meshBuffers.end()) return;

        glUseProgram(shadowProg);
        if (uShadowUseSkinningLoc != -1) glUniform1i(uShadowUseSkinningLoc, 0);
        Mat4 mvp = cascadeMatrices[activeShadowCascade] * worldMat;
        glUniformMatrix4fv(uShadowMVPLoc, 1, GL_TRUE, &mvp.m[0][0]);
        glBindVertexArray(it->second.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(it->second.indexCount), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    template <typename SkelT>
    void RenderSkinnedMeshShadow(const SkelT& skel, const Mat4& worldMat) {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || !skel.activeDeformedMesh || shadowFBO == 0 || shadowProg == 0) return;
        if (!EnsureSkinnedMeshUploaded(skel.activeDeformedMesh, skel.bindVertices)) return;
        auto it = meshBuffers.find(skel.activeDeformedMesh.get());
        if (it == meshBuffers.end()) return;

        UploadBonePalette(skel.skinMatrices);
        glUseProgram(shadowProg);
        if (uShadowUseSkinningLoc != -1) glUniform1i(uShadowUseSkinningLoc, 1);
        if (uShadowFootOffsetLoc != -1) glUniform1f(uShadowFootOffsetLoc, skel.footBaseOffset);
        Mat4 mvp = cascadeMatrices[activeShadowCascade] * worldMat;
        glUniformMatrix4fv(uShadowMVPLoc, 1, GL_TRUE, &mvp.m[0][0]);
        glBindVertexArray(it->second.vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(it->second.indexCount), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        if (uShadowUseSkinningLoc != -1) glUniform1i(uShadowUseSkinningLoc, 0);
    }

    void EndShadowPass() {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || shadowFBO == 0) return;
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenWidth, screenHeight);
        lightSpaceMatrix = cascadeMatrices[0];
        shadowMapReady = true;
    }

    /**
     * Resize GL viewport + HDR/Bloom/screen/HUD textures (window resize / fullscreen toggle).
     */
    void Resize(uint32_t w, uint32_t h) {
        if (w == 0 || h == 0) return;
        screenWidth = w;
        screenHeight = h;
        if (!isGLInitialized) return;

        glBindTexture(GL_TEXTURE_2D, screenTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenWidth, screenHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, hudTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenWidth, screenHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        ResizeHDRTargets();
        glViewport(0, 0, screenWidth, screenHeight);
    }

    void ToggleBackend() {
        if (!isGLInitialized) {
            backend = RenderBackend::CPU_AVX2;
            return;
        }
        if (backend == RenderBackend::GPU_OPENGL) {
            backend = RenderBackend::CPU_AVX2;
        } else {
            backend = RenderBackend::GPU_OPENGL;
        }
    }

    std::string GetBackendName() const {
        if (backend == RenderBackend::GPU_OPENGL) {
            return "GPU OPENGL 4.6 [HARDWARE ACCEL]";
        } else {
            return "CPU BARE-METAL [AVX2 SOFTWARE SIMD]";
        }
    }

    void BeginFrame(const ColorRGB& clearColor = ColorRGB(0.02f, 0.03f, 0.07f)) {
        if (backend == RenderBackend::GPU_OPENGL && isGLInitialized) {
            if (hdrEnabled && hdrPipelineReady && hdrFBO != 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
                hdrSceneOpen = true;
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                hdrSceneOpen = false;
            }
            glViewport(0, 0, screenWidth, screenHeight);
            glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_CULL_FACE);

            glUseProgram(pbrProg);
            if (uOutputLinearHDRLoc != -1) glUniform1i(uOutputLinearHDRLoc, hdrSceneOpen ? 1 : 0);
            if (uUseSkinningLoc != -1) glUniform1i(uUseSkinningLoc, 0);
            if (uAllowWetPuddlesLoc != -1) glUniform1i(uAllowWetPuddlesLoc, 0);
        }
    }

    void EndHDRScenePass() {
        if (!isGLInitialized || !hdrSceneOpen) return;
        hdrSceneOpen = false;

        if (!hdrPipelineReady || hdrFBO == 0 || bloomExtractProg == 0 || bloomBlurProg == 0 || hdrCompositeProg == 0 ||
            bloomFBO[0] == 0 || bloomFBO[1] == 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, screenWidth, screenHeight);
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        // 1. Bright-pass bloom extraction (half-res)
        glViewport(0, 0, bloomWidth, bloomHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[0]);
        glUseProgram(bloomExtractProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColorTex);
        if (uBloomThresholdLoc != -1) glUniform1f(uBloomThresholdLoc, bloomThreshold);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 2. Separable Gaussian ping-pong blur
        bool horizontal = true;
        int passes = (shaderQuality >= 2) ? 6 : 4;
        glUseProgram(bloomBlurProg);
        for (int i = 0; i < passes; ++i) {
            int dstIdx = horizontal ? 1 : 0;
            int srcIdx = 1 - dstIdx;
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[dstIdx]);
            if (uBloomBlurHorizLoc != -1) glUniform1i(uBloomBlurHorizLoc, horizontal ? 1 : 0);
            glBindTexture(GL_TEXTURE_2D, bloomTex[srcIdx]);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            horizontal = !horizontal;
        }

        // 3. Final HDR + Bloom -> ACES Filmic + sRGB Composite into Default Framebuffer 0
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenWidth, screenHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(hdrCompositeProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdrColorTex);
        if (uCompositeHdrLoc != -1) glUniform1i(uCompositeHdrLoc, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
        if (uCompositeBloomLoc != -1) glUniform1i(uCompositeBloomLoc, 1);
        if (uCompositeBloomIntLoc != -1) glUniform1f(uCompositeBloomIntLoc, (shaderQuality == 0) ? 0.0f : bloomIntensity);
        if (uCompositeExposureLoc != -1) glUniform1f(uCompositeExposureLoc, exposure);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

    void SetGlobalUniforms(
        const Vec3& sunDir,
        const ColorRGB& sunColor,
        const ColorRGB& ambientColor,
        const Vec3& camPos,
        const ColorRGB& fogColor = ColorRGB(0.02f, 0.03f, 0.05f),
        float timeSec = 0.0f,
        const std::vector<PointLight>& pointLights = {})
    {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized) return;

        glUseProgram(pbrProg);
        glUniform3f(uSunDirLoc, sunDir.x, sunDir.y, sunDir.z);
        glUniform3f(uSunColorLoc, sunColor.r, sunColor.g, sunColor.b);
        glUniform3f(uAmbientColorLoc, ambientColor.r, ambientColor.g, ambientColor.b);
        glUniform3f(uCamPosLoc, camPos.x, camPos.y, camPos.z);
        if (uOutputLinearHDRLoc != -1) {
            glUniform1i(uOutputLinearHDRLoc, hdrSceneOpen ? 1 : 0);
        }
        if (uFogColorLoc != -1) {
            glUniform3f(uFogColorLoc, fogColor.r, fogColor.g, fogColor.b);
        }
        if (uTimeLoc != -1) {
            glUniform1f(uTimeLoc, timeSec);
        }
        if (uBeatPulseLoc != -1) {
            float bpmClamped = std::max(60.0f, std::min(220.0f, activeBPM));
            float beatFreqRad = (bpmClamped / 60.0f) * 6.2831853f;
            float rawWave = 0.5f + 0.5f * std::sin(timeSec * beatFreqRad);
            beatPulse = std::pow(rawWave, 1.8f);
            glUniform1f(uBeatPulseLoc, beatPulse);
        }
        if (uLightSpaceMatrixLoc != -1) {
            glUniformMatrix4fv(uLightSpaceMatrixLoc, NUM_SHADOW_CASCADES, GL_TRUE, &cascadeMatrices[0].m[0][0]);
        }
        if (uCascadeSplitsLoc != -1) {
            glUniform3f(uCascadeSplitsLoc, cascadeSplits[0], cascadeSplits[1], cascadeSplits[2]);
        }
        if (uNumShadowCascadesLoc != -1) {
            glUniform1i(uNumShadowCascadesLoc, std::max(1, renderedShadowCascades));
        }
        if (uShadowMapEnabledLoc != -1 && shadowDepthTex != 0 && shadowMapReady) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D_ARRAY, shadowDepthTex);
            glUniform1i(uShadowMapLoc, 1);
            glUniform1i(uShadowMapEnabledLoc, 1);
            glActiveTexture(GL_TEXTURE0);
        } else if (uShadowMapEnabledLoc != -1) {
            glUniform1i(uShadowMapEnabledLoc, 0);
        }
        if (uWetnessLoc != -1) {
            glUniform1f(uWetnessLoc, wetness);
        }
        if (uGlowIntensityLoc != -1) {
            glUniform1f(uGlowIntensityLoc, glowIntensity);
        }
        if (uShaderQualityLoc != -1) {
            glUniform1i(uShaderQualityLoc, shaderQuality);
        }
        if (uNumPointLightsLoc != -1) {
            int numLights = std::min(static_cast<int>(pointLights.size()), 8);
            glUniform1i(uNumPointLightsLoc, numLights);
            if (numLights > 0) {
                float posArray[24];
                float colArray[24];
                float radArray[8];
                for (int i = 0; i < numLights; ++i) {
                    posArray[i * 3 + 0] = pointLights[i].position.x;
                    posArray[i * 3 + 1] = pointLights[i].position.y;
                    posArray[i * 3 + 2] = pointLights[i].position.z;

                    float intens = (pointLights[i].intensity > 0.0f ? pointLights[i].intensity : 1.0f) * glowIntensity;
                    colArray[i * 3 + 0] = pointLights[i].color.r * intens;
                    colArray[i * 3 + 1] = pointLights[i].color.g * intens;
                    colArray[i * 3 + 2] = pointLights[i].color.b * intens;

                    radArray[i] = pointLights[i].radius > 0.0f ? pointLights[i].radius : 20.0f;
                }
                glUniform3fv(uPointLightPosLoc, numLights, posArray);
                glUniform3fv(uPointLightColLoc, numLights, colArray);
                glUniform1fv(uPointLightRadiusLoc, numLights, radArray);
            }
        }
    }

    void DestroyMesh(const SurrealMesh* meshPtr) {
        if (!meshPtr || !isGLInitialized) return;
        auto it = meshBuffers.find(meshPtr);
        if (it != meshBuffers.end()) {
            if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
            if (it->second.ebo) glDeleteBuffers(1, &it->second.ebo);
            if (it->second.skinVbo) glDeleteBuffers(1, &it->second.skinVbo);
            if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
            meshBuffers.erase(it);
        }
    }

    void DeleteTextureChecked(GLuint& texId) {
        if (texId != 0) {
            if (isGLInitialized) glDeleteTextures(1, &texId);
            ownedTextures.erase(texId);
            texId = 0;
        }
    }

    void DestroyMeshWithTextures(SurrealMesh* meshPtr) {
        if (!meshPtr) return;
        for (auto& sub : meshPtr->submeshes) {
            DeleteTextureChecked(sub.textureId);
            DeleteTextureChecked(sub.normalTextureId);
            DeleteTextureChecked(sub.mrTextureId);
            DeleteTextureChecked(sub.aoTextureId);
            DeleteTextureChecked(sub.emissiveTextureId);
        }
        DestroyMesh(meshPtr);
    }

    void UploadBonePalette(const std::vector<Mat4>& skinMatrices) {
        if (boneUBO == 0 || skinMatrices.empty()) return;
        int count = std::min(static_cast<int>(skinMatrices.size()), MAX_GPU_BONES);
        std::vector<float> packed(static_cast<size_t>(count) * 16);
        for (int b = 0; b < count; ++b) {
            const Mat4& m = skinMatrices[b];
            float* dst = &packed[static_cast<size_t>(b) * 16];
            // Transpose C++ row-major Mat4 into GLSL std140 column-major mat4
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    dst[c * 4 + r] = m.m[r][c];
                }
            }
        }
        glBindBuffer(GL_UNIFORM_BUFFER, boneUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(count * 16 * sizeof(float)), packed.data());
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, boneUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    template <typename SkinnedVertT>
    bool EnsureSkinnedMeshUploaded(
        const std::shared_ptr<SurrealMesh>& mesh,
        const std::vector<SkinnedVertT>& bindVerts)
    {
        if (!mesh || bindVerts.empty()) return false;
        const SurrealMesh* meshKey = mesh.get();
        auto it = meshBuffers.find(meshKey);

        std::vector<Vertex3D> bindV3D;
        bool needVboUpload = false;
        bool needSkinUpload = false;

        if (it == meshBuffers.end()) {
            GLMeshBuffer buf;
            buf.vertexCount = bindVerts.size();
            buf.indexCount = mesh->indices.size();
            buf.skinVertexCount = bindVerts.size();
            buf.hasBindPoseVBO = true;

            bindV3D.resize(bindVerts.size());
            std::vector<GPUSkinVertex> skinData(bindVerts.size());
            for (size_t i = 0; i < bindVerts.size(); ++i) {
                bindV3D[i] = Vertex3D(bindVerts[i].position, bindVerts[i].normal, bindVerts[i].u, bindVerts[i].v, bindVerts[i].color);
                for (int k = 0; k < 4; ++k) {
                    skinData[i].joints[k] = static_cast<uint32_t>(bindVerts[i].joints[k]);
                    skinData[i].weights[k] = bindVerts[i].weights[k];
                }
            }

            glGenVertexArrays(1, &buf.vao);
            glBindVertexArray(buf.vao);

            glGenBuffers(1, &buf.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
            glBufferData(GL_ARRAY_BUFFER, bindV3D.size() * sizeof(Vertex3D), bindV3D.data(), GL_STATIC_DRAW);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, u));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));
            glEnableVertexAttribArray(3);

            glGenBuffers(1, &buf.skinVbo);
            glBindBuffer(GL_ARRAY_BUFFER, buf.skinVbo);
            glBufferData(GL_ARRAY_BUFFER, skinData.size() * sizeof(GPUSkinVertex), skinData.data(), GL_STATIC_DRAW);

            glVertexAttribIPointer(4, 4, GL_UNSIGNED_INT, sizeof(GPUSkinVertex), (void*)offsetof(GPUSkinVertex, joints));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(GPUSkinVertex), (void*)offsetof(GPUSkinVertex, weights));
            glEnableVertexAttribArray(5);

            glGenBuffers(1, &buf.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indices.size() * sizeof(uint32_t), mesh->indices.data(), GL_STATIC_DRAW);

            glBindVertexArray(0);
            meshBuffers[meshKey] = buf;
            return true;
        }

        if (!it->second.hasBindPoseVBO || it->second.vertexCount != bindVerts.size()) needVboUpload = true;
        if (it->second.skinVbo == 0 || it->second.skinVertexCount != bindVerts.size()) needSkinUpload = true;
        if (!needVboUpload && !needSkinUpload) return true;

        glBindVertexArray(it->second.vao);
        if (needVboUpload) {
            bindV3D.resize(bindVerts.size());
            for (size_t i = 0; i < bindVerts.size(); ++i) {
                bindV3D[i] = Vertex3D(bindVerts[i].position, bindVerts[i].normal, bindVerts[i].u, bindVerts[i].v, bindVerts[i].color);
            }
            glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo);
            glBufferData(GL_ARRAY_BUFFER, bindV3D.size() * sizeof(Vertex3D), bindV3D.data(), GL_STATIC_DRAW);
            it->second.vertexCount = bindVerts.size();
            it->second.hasBindPoseVBO = true;
        }
        if (needSkinUpload) {
            std::vector<GPUSkinVertex> skinData(bindVerts.size());
            for (size_t i = 0; i < bindVerts.size(); ++i) {
                for (int k = 0; k < 4; ++k) {
                    skinData[i].joints[k] = static_cast<uint32_t>(bindVerts[i].joints[k]);
                    skinData[i].weights[k] = bindVerts[i].weights[k];
                }
            }
            if (it->second.skinVbo == 0) glGenBuffers(1, &it->second.skinVbo);
            glBindBuffer(GL_ARRAY_BUFFER, it->second.skinVbo);
            glBufferData(GL_ARRAY_BUFFER, skinData.size() * sizeof(GPUSkinVertex), skinData.data(), GL_STATIC_DRAW);
            glVertexAttribIPointer(4, 4, GL_UNSIGNED_INT, sizeof(GPUSkinVertex), (void*)offsetof(GPUSkinVertex, joints));
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(GPUSkinVertex), (void*)offsetof(GPUSkinVertex, weights));
            glEnableVertexAttribArray(5);
            it->second.skinVertexCount = bindVerts.size();
        }
        glBindVertexArray(0);
        return true;
    }

    void EnsureMeshUploaded(
        const std::shared_ptr<SurrealMesh>& mesh,
        bool isDynamic = false,
        bool uploadDynamicVBO = true)
    {
        if (!mesh) return;
        const SurrealMesh* meshKey = mesh.get();
        auto it = meshBuffers.find(meshKey);

        if (it == meshBuffers.end()) {
            GLMeshBuffer buf;
            buf.vertexCount = mesh->vertices.size();
            buf.indexCount = mesh->indices.size();
            buf.hasBindPoseVBO = false;

            glGenVertexArrays(1, &buf.vao);
            glBindVertexArray(buf.vao);

            glGenBuffers(1, &buf.vbo);
            glBindBuffer(GL_ARRAY_BUFFER, buf.vbo);
            glBufferData(GL_ARRAY_BUFFER, mesh->vertices.size() * sizeof(Vertex3D),
                         mesh->vertices.data(), isDynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

            glGenBuffers(1, &buf.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indices.size() * sizeof(uint32_t),
                         mesh->indices.data(), GL_STATIC_DRAW);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, u));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));
            glEnableVertexAttribArray(3);

            glDisableVertexAttribArray(4);
            glDisableVertexAttribArray(5);
            glBindVertexArray(0);

            meshBuffers[meshKey] = buf;
        } else if (isDynamic && uploadDynamicVBO) {
            it->second.hasBindPoseVBO = false;
            glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo);
            if (it->second.vertexCount == mesh->vertices.size() && it->second.indexCount == mesh->indices.size()) {
                glBufferSubData(GL_ARRAY_BUFFER, 0, mesh->vertices.size() * sizeof(Vertex3D), mesh->vertices.data());
            } else {
                it->second.vertexCount = mesh->vertices.size();
                it->second.indexCount = mesh->indices.size();
                glBufferData(GL_ARRAY_BUFFER, mesh->vertices.size() * sizeof(Vertex3D), mesh->vertices.data(), GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indices.size() * sizeof(uint32_t), mesh->indices.data(), GL_STATIC_DRAW);
            }
        }
    }

    GLuint EnsureSubTextureUploaded(
        GLuint& texId,
        int w, int h,
        const std::shared_ptr<std::vector<uint8_t>>& rgba,
        bool isSRGB)
    {
        if (texId != 0) return texId;
        if (!rgba || rgba->empty() || w <= 0 || h <= 0) return 0;

        glGenTextures(1, &texId);
        ownedTextures.insert(texId);
        glBindTexture(GL_TEXTURE_2D, texId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.65f);
        glTexParameterf(GL_TEXTURE_2D, 0x84FE, 16.0f);
        GLint internalFmt = isSRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->data());
        glGenerateMipmap(GL_TEXTURE_2D);
        return texId;
    }

    void RenderMesh(
        const std::shared_ptr<SurrealMesh>& mesh,
        const Mat4& worldMat,
        const Mat4& vpMat,
        const PBRMaterial& material,
        bool isDynamic = false,
        bool uploadDynamicVBO = true)
    {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || !mesh) return;

        EnsureMeshUploaded(mesh, isDynamic, uploadDynamicVBO);
        auto it = meshBuffers.find(mesh.get());
        if (it == meshBuffers.end()) return;

        glUseProgram(pbrProg);
        if (uUseSkinningLoc != -1) glUniform1i(uUseSkinningLoc, 0);
        if (uFootOffsetLoc != -1) glUniform1f(uFootOffsetLoc, 0.0f);
        if (uAllowWetPuddlesLoc != -1) glUniform1i(uAllowWetPuddlesLoc, material.allowWetPuddles ? 1 : 0);

        DrawMeshInternal(mesh, it->second, worldMat, vpMat, material);
    }

    template <typename SkelT>
    void RenderSkinnedMesh(
        const SkelT& skel,
        const Mat4& worldMat,
        const Mat4& vpMat,
        const PBRMaterial& material)
    {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || !skel.activeDeformedMesh) return;
        if (!EnsureSkinnedMeshUploaded(skel.activeDeformedMesh, skel.bindVertices)) return;
        auto it = meshBuffers.find(skel.activeDeformedMesh.get());
        if (it == meshBuffers.end()) return;

        UploadBonePalette(skel.skinMatrices);
        glUseProgram(pbrProg);
        if (uUseSkinningLoc != -1) glUniform1i(uUseSkinningLoc, 1);
        if (uFootOffsetLoc != -1) glUniform1f(uFootOffsetLoc, skel.footBaseOffset);
        if (uAllowWetPuddlesLoc != -1) glUniform1i(uAllowWetPuddlesLoc, material.allowWetPuddles ? 1 : 0);

        DrawMeshInternal(skel.activeDeformedMesh, it->second, worldMat, vpMat, material);

        if (uUseSkinningLoc != -1) glUniform1i(uUseSkinningLoc, 0);
    }

private:
    void DrawMeshInternal(
        const std::shared_ptr<SurrealMesh>& mesh,
        const GLMeshBuffer& buf,
        const Mat4& worldMat,
        const Mat4& vpMat,
        const PBRMaterial& material)
    {
        Mat4 mvpMat = vpMat * worldMat;
        glUniformMatrix4fv(uMVPLoc, 1, GL_TRUE, &mvpMat.m[0][0]);
        glUniformMatrix4fv(uModelLoc, 1, GL_TRUE, &worldMat.m[0][0]);
        if (uNormalMatLoc != -1) {
            Mat4 nm = worldMat.InverseTranspose3x3();
            float nMat[9] = {
                nm.m[0][0], nm.m[0][1], nm.m[0][2],
                nm.m[1][0], nm.m[1][1], nm.m[1][2],
                nm.m[2][0], nm.m[2][1], nm.m[2][2]
            };
            glUniformMatrix3fv(uNormalMatLoc, 1, GL_TRUE, nMat);
        }

        glBindVertexArray(buf.vao);

        if (mesh->submeshes.empty()) {
            if (material.doubleSided) glDisable(GL_CULL_FACE);
            else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }

            glUniform1i(uHasTextureLoc, 0);
            if (uHasNormalMapLoc != -1) glUniform1i(uHasNormalMapLoc, 0);
            if (uHasMRMapLoc != -1) glUniform1i(uHasMRMapLoc, 0);
            if (uHasAOMapLoc != -1) glUniform1i(uHasAOMapLoc, 0);
            if (uHasEmissiveMapLoc != -1) glUniform1i(uHasEmissiveMapLoc, 0);
            if (uAlphaModeLoc != -1) glUniform1i(uAlphaModeLoc, static_cast<int>(material.alphaMode));
            if (uAlphaCutoffLoc != -1) glUniform1f(uAlphaCutoffLoc, material.alphaCutoff);
            glUniform3f(uAlbedoLoc, material.albedo.r, material.albedo.g, material.albedo.b);
            glUniform1f(uMetallicLoc, material.metallic);
            glUniform1f(uRoughnessLoc, material.roughness);
            if (uAmbientOcclusionLoc != -1) glUniform1f(uAmbientOcclusionLoc, material.ambientOcclusion);
            if (uEmissiveLoc != -1) {
                glUniform3f(uEmissiveLoc, material.emissive.r, material.emissive.g, material.emissive.b);
            }
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(buf.indexCount), GL_UNSIGNED_INT, 0);
            glDisable(GL_CULL_FACE);
            glBindVertexArray(0);
            return;
        }

        auto drawSubmesh = [&](SubMesh& sub) {
            bool ds = sub.doubleSided || material.doubleSided;
            if (ds) glDisable(GL_CULL_FACE);
            else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }

            GLuint albedoTex = EnsureSubTextureUploaded(sub.textureId, sub.texWidth, sub.texHeight, sub.texRGBA, true);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, albedoTex);
            if (uAlbedoMapLoc != -1) glUniform1i(uAlbedoMapLoc, 0);
            glUniform1i(uHasTextureLoc, albedoTex != 0 ? 1 : 0);

            GLuint normTex = EnsureSubTextureUploaded(sub.normalTextureId, sub.normalTexW, sub.normalTexH, sub.normalTexRGBA, false);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, normTex);
            if (uNormalMapLoc != -1) glUniform1i(uNormalMapLoc, 2);
            if (uHasNormalMapLoc != -1) glUniform1i(uHasNormalMapLoc, normTex != 0 ? 1 : 0);

            GLuint mrTex = EnsureSubTextureUploaded(sub.mrTextureId, sub.mrTexW, sub.mrTexH, sub.mrTexRGBA, false);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, mrTex);
            if (uMRMapLoc != -1) glUniform1i(uMRMapLoc, 3);
            if (uHasMRMapLoc != -1) glUniform1i(uHasMRMapLoc, mrTex != 0 ? 1 : 0);

            GLuint aoTex = EnsureSubTextureUploaded(sub.aoTextureId, sub.aoTexW, sub.aoTexH, sub.aoTexRGBA, false);
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, aoTex);
            if (uAOMapLoc != -1) glUniform1i(uAOMapLoc, 4);
            if (uHasAOMapLoc != -1) glUniform1i(uHasAOMapLoc, aoTex != 0 ? 1 : 0);

            GLuint emTex = EnsureSubTextureUploaded(sub.emissiveTextureId, sub.emissiveTexW, sub.emissiveTexH, sub.emissiveTexRGBA, true);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, emTex);
            if (uEmissiveMapLoc != -1) glUniform1i(uEmissiveMapLoc, 5);
            if (uHasEmissiveMapLoc != -1) glUniform1i(uHasEmissiveMapLoc, emTex != 0 ? 1 : 0);

            if (uAlphaModeLoc != -1) glUniform1i(uAlphaModeLoc, static_cast<int>(sub.alphaMode));
            if (uAlphaCutoffLoc != -1) glUniform1f(uAlphaCutoffLoc, sub.alphaCutoff);

            ColorRGB effectiveAlbedo = sub.albedo * material.albedo;
            glUniform3f(uAlbedoLoc, effectiveAlbedo.r, effectiveAlbedo.g, effectiveAlbedo.b);
            glUniform1f(uMetallicLoc, sub.metallic);
            glUniform1f(uRoughnessLoc, sub.roughness);
            if (uAmbientOcclusionLoc != -1) glUniform1f(uAmbientOcclusionLoc, sub.ambientOcclusion * material.ambientOcclusion);
            if (uEmissiveLoc != -1) {
                ColorRGB effectiveEmissive = sub.emissive;
                if (effectiveEmissive.r == 0 && effectiveEmissive.g == 0 && effectiveEmissive.b == 0) {
                    effectiveEmissive = material.emissive;
                }
                glUniform3f(uEmissiveLoc, effectiveEmissive.r, effectiveEmissive.g, effectiveEmissive.b);
            }

            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(static_cast<uintptr_t>(sub.indexOffset * sizeof(uint32_t))));
        };

        // Pass 1: Opaque and Alpha-Mask submeshes
        bool hasBlendSubmeshes = false;
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        for (auto& sub : mesh->submeshes) {
            if (sub.indexCount == 0) continue;
            if (sub.alphaMode == PBRMaterial::AlphaMode::BLEND_MODE) {
                hasBlendSubmeshes = true;
                continue;
            }
            drawSubmesh(sub);
        }

        // Pass 2: Alpha-Blended submeshes (glass, translucent hair/fabric)
        if (hasBlendSubmeshes) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            for (auto& sub : mesh->submeshes) {
                if (sub.indexCount == 0 || sub.alphaMode != PBRMaterial::AlphaMode::BLEND_MODE) continue;
                drawSubmesh(sub);
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glActiveTexture(GL_TEXTURE0);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(0);
    }

public:

    void BlitSoftwareFramebuffer(const uint32_t* fb, int w, int h) {
        if (!isGLInitialized || !fb) return;
        hdrSceneOpen = false;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenWidth, screenHeight);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, screenTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, fb);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(quadProg);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void BlitHUDOverlay(const uint32_t* hudFb, int w, int h, bool uploadNewPixels = true) {
        if (!isGLInitialized || !hudFb) return;
        if (hdrSceneOpen) EndHDRScenePass();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenWidth, screenHeight);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hudTex);
        if (uploadNewPixels) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, hudFb);
        }

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(quadProg);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    /**
     * Render Celestial Sky Dome (Deep atmosphere, horizon gradient, sun/moon disc & stars)
     */
    void RenderSky(
        const Mat4& invViewProj,
        const ColorRGB& zenithCol,
        const ColorRGB& horizonCol,
        const ColorRGB& groundCol,
        const Vec3& sunDir,
        const ColorRGB& sunCol,
        bool isNight)
    {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || skyProg == 0) return;

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glUseProgram(skyProg);

        glUniformMatrix4fv(uSkyInvVPLoc, 1, GL_TRUE, &invViewProj.m[0][0]);
        glUniform3f(uSkyZenithLoc, zenithCol.r, zenithCol.g, zenithCol.b);
        glUniform3f(uSkyHorizonLoc, horizonCol.r, horizonCol.g, horizonCol.b);
        glUniform3f(uSkyGroundLoc, groundCol.r, groundCol.g, groundCol.b);
        glUniform3f(uSkySunDirLoc, sunDir.x, sunDir.y, sunDir.z);
        glUniform3f(uSkySunColLoc, sunCol.r, sunCol.g, sunCol.b);
        glUniform1f(uSkyIsNightLoc, isNight ? 1.0f : 0.0f);
        if (uSkyOutputLinearHDRLoc != -1) glUniform1i(uSkyOutputLinearHDRLoc, hdrSceneOpen ? 1 : 0);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
    }

    void Present() {
        if (isGLInitialized && window) {
            if (hdrSceneOpen) EndHDRScenePass();
            SDL_GL_SwapWindow(window);
        }
    }

    void Cleanup() {
        if (!isGLInitialized && !glContext) return;

        for (auto& pair : meshBuffers) {
            if (pair.second.vbo) glDeleteBuffers(1, &pair.second.vbo);
            if (pair.second.ebo) glDeleteBuffers(1, &pair.second.ebo);
            if (pair.second.skinVbo) glDeleteBuffers(1, &pair.second.skinVbo);
            if (pair.second.vao) glDeleteVertexArrays(1, &pair.second.vao);
        }
        meshBuffers.clear();

        if (!ownedTextures.empty()) {
            std::vector<GLuint> texList(ownedTextures.begin(), ownedTextures.end());
            glDeleteTextures(static_cast<GLsizei>(texList.size()), texList.data());
            ownedTextures.clear();
        }

        if (boneUBO) { glDeleteBuffers(1, &boneUBO); boneUBO = 0; }
        if (screenTex) { glDeleteTextures(1, &screenTex); screenTex = 0; }
        if (hudTex) { glDeleteTextures(1, &hudTex); hudTex = 0; }
        if (shadowDepthTex) { glDeleteTextures(1, &shadowDepthTex); shadowDepthTex = 0; }
        if (shadowFBO) { glDeleteFramebuffers(1, &shadowFBO); shadowFBO = 0; }
        if (hdrColorTex) { glDeleteTextures(1, &hdrColorTex); hdrColorTex = 0; }
        if (hdrDepthRBO) { glDeleteRenderbuffers(1, &hdrDepthRBO); hdrDepthRBO = 0; }
        if (hdrFBO) { glDeleteFramebuffers(1, &hdrFBO); hdrFBO = 0; }
        for (int i = 0; i < 2; ++i) {
            if (bloomTex[i]) { glDeleteTextures(1, &bloomTex[i]); bloomTex[i] = 0; }
            if (bloomFBO[i]) { glDeleteFramebuffers(1, &bloomFBO[i]); bloomFBO[i] = 0; }
        }
        hdrPipelineReady = false;
        hdrSceneOpen = false;
        if (quadVAO) { glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
        if (quadVBO) { glDeleteBuffers(1, &quadVBO); quadVBO = 0; }
        if (pbrProg) { glDeleteProgram(pbrProg); pbrProg = 0; }
        if (quadProg) { glDeleteProgram(quadProg); quadProg = 0; }
        if (skyProg) { glDeleteProgram(skyProg); skyProg = 0; }
        if (shadowProg) { glDeleteProgram(shadowProg); shadowProg = 0; }
        if (bloomExtractProg) { glDeleteProgram(bloomExtractProg); bloomExtractProg = 0; }
        if (bloomBlurProg) { glDeleteProgram(bloomBlurProg); bloomBlurProg = 0; }
        if (hdrCompositeProg) { glDeleteProgram(hdrCompositeProg); hdrCompositeProg = 0; }

        if (glContext) {
            SDL_GL_DeleteContext(glContext);
            glContext = nullptr;
        }
        isGLInitialized = false;
        shadowMapReady = false;
    }

private:
    bool LinkProgram(GLuint prog, const char* label) {
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            std::cerr << "[-] Shader link error (" << label << "): " << log << "\n";
            return false;
        }
        return true;
    }

    void BindBoneBlock(GLuint prog) {
        if (prog == 0) return;
        GLuint idx = glGetUniformBlockIndex(prog, "BoneBlock");
        if (idx != GL_INVALID_INDEX) {
            glUniformBlockBinding(prog, idx, 0);
        }
    }

    bool BuildShaders() {
        // 0. Directional Shadow Depth Pass Shader (2048x2048x3 CSM + GPU Skinning)
        const char* shadowVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 4) in uvec4 aJoints;
layout (location = 5) in vec4 aWeights;

layout(std140) uniform BoneBlock {
    mat4 uBones[250];
};

uniform mat4 uShadowMVP;
uniform int uShadowUseSkinning;
uniform float uShadowFootOffset;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    if (uShadowUseSkinning == 1) {
        mat4 skinMat =
            aWeights.x * uBones[min(aJoints.x, 249u)] +
            aWeights.y * uBones[min(aJoints.y, 249u)] +
            aWeights.z * uBones[min(aJoints.z, 249u)] +
            aWeights.w * uBones[min(aJoints.w, 249u)];
        localPos = skinMat * localPos;
        localPos.y -= uShadowFootOffset;
    }
    gl_Position = uShadowMVP * localPos;
}
)";
        const char* shadowFS = R"(
#version 330 core
void main() {
    // Hardware depth written automatically
}
)";
        GLuint shvs = CompileShader(GL_VERTEX_SHADER, shadowVS);
        GLuint shfs = CompileShader(GL_FRAGMENT_SHADER, shadowFS);
        if (shvs && shfs) {
            shadowProg = glCreateProgram();
            glAttachShader(shadowProg, shvs);
            glAttachShader(shadowProg, shfs);
            bool linked = LinkProgram(shadowProg, "shadowProg");
            glDeleteShader(shvs);
            glDeleteShader(shfs);
            if (!linked) { glDeleteProgram(shadowProg); shadowProg = 0; return false; }
            BindBoneBlock(shadowProg);
            uShadowMVPLoc = glGetUniformLocation(shadowProg, "uShadowMVP");
            uShadowUseSkinningLoc = glGetUniformLocation(shadowProg, "uShadowUseSkinning");
            uShadowFootOffsetLoc = glGetUniformLocation(shadowProg, "uShadowFootOffset");
        }

        // 1. Cook-Torrance PBR Vertex Shader (GPU LBS Skinning + 3-Cascade Light Space Projection)
        const char* pbrVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;
layout (location = 4) in uvec4 aJoints;
layout (location = 5) in vec4 aWeights;

layout(std140) uniform BoneBlock {
    mat4 uBones[250];
};

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uLightSpaceMatrix[3];
uniform int uUseSkinning;
uniform float uFootOffset;
uniform vec3 uCamPos;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vColor;
out vec4 vFragPosLightSpace0;
out vec4 vFragPosLightSpace1;
out vec4 vFragPosLightSpace2;
out float vViewDist;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    vec3 localNorm = aNormal;
    if (uUseSkinning == 1) {
        mat4 skinMat =
            aWeights.x * uBones[min(aJoints.x, 249u)] +
            aWeights.y * uBones[min(aJoints.y, 249u)] +
            aWeights.z * uBones[min(aJoints.z, 249u)] +
            aWeights.w * uBones[min(aJoints.w, 249u)];
        localPos = skinMat * localPos;
        localPos.y -= uFootOffset;
        localNorm = mat3(skinMat) * localNorm;
    }
    vec4 worldPos = uModel * localPos;
    vFragPos = vec3(worldPos);
    vNormal = normalize(uNormalMatrix * localNorm);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vFragPosLightSpace0 = uLightSpaceMatrix[0] * worldPos;
    vFragPosLightSpace1 = uLightSpaceMatrix[1] * worldPos;
    vFragPosLightSpace2 = uLightSpaceMatrix[2] * worldPos;
    vViewDist = length(uCamPos - vFragPos);
    gl_Position = uMVP * localPos;
}
)";

        // 2. Cook-Torrance PBR Fragment Shader (PBR Maps + 3-Cascade PCF + Opt-In Wet Puddles + Linear HDR)
        const char* pbrFS = R"(
#version 330 core
out vec4 FragColor;

in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in vec4 vFragPosLightSpace0;
in vec4 vFragPosLightSpace1;
in vec4 vFragPosLightSpace2;
in float vViewDist;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uCamPos;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAmbientOcclusion;
uniform vec3 uEmissive;

uniform sampler2D uAlbedoMap;
uniform int uHasTexture;
uniform sampler2D uNormalMap;
uniform int uHasNormalMap;
uniform sampler2D uMRMap;
uniform int uHasMRMap;
uniform sampler2D uAOMap;
uniform int uHasAOMap;
uniform sampler2D uEmissiveMap;
uniform int uHasEmissiveMap;

uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform int uAllowWetPuddles;
uniform int uOutputLinearHDR;

uniform sampler2DArray uShadowMap;
uniform int uShadowMapEnabled;
uniform vec3 uCascadeSplits;
uniform int uNumShadowCascades;
uniform float uBeatPulse;
uniform vec3 uFogColor;
uniform float uTime;
uniform float uWetness;
uniform float uGlowIntensity;
uniform int uShaderQuality;

const int MAX_POINT_LIGHTS = 8;
uniform int uNumPointLights;
uniform vec3 uPointLightPos[8];
uniform vec3 uPointLightCol[8];
uniform float uPointLightRadius[8];

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / max(denom, 0.000001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / max(denom, 0.000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 PerturbNormal(vec3 N, vec3 worldPos, vec2 uv) {
    vec3 mapN = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    if (dot(T, T) < 1e-8 && dot(B, B) < 1e-8) return N;
    return normalize(mat3(T * invmax, B * invmax, N) * mapN);
}

void main() {
    float alpha = vColor.a;
    vec3 baseAlbedo = uAlbedo;
    vec4 texSample = vec4(1.0);
    if (uHasTexture == 1) {
        texSample = texture(uAlbedoMap, vTexCoord);
        float cutoff = (uAlphaMode == 1) ? uAlphaCutoff : 0.15;
        if (uAlphaMode != 2 && texSample.a < cutoff) discard;
        baseAlbedo *= texSample.rgb;
        alpha *= texSample.a;
    } else {
        baseAlbedo *= vColor.rgb;
    }
    vec3 albedo = baseAlbedo;

    float baseRoughness = clamp(uRoughness, 0.04, 1.0);
    float baseMetallic = clamp(uMetallic, 0.0, 1.0);
    if (uHasMRMap == 1) {
        vec4 mr = texture(uMRMap, vTexCoord);
        baseRoughness = clamp(baseRoughness * mr.g, 0.04, 1.0);
        baseMetallic = clamp(baseMetallic * mr.b, 0.0, 1.0);
    }

    float baseAO = clamp(uAmbientOcclusion, 0.0, 1.0);
    if (uHasAOMap == 1) {
        baseAO *= texture(uAOMap, vTexCoord).r;
    }

    vec3 N = normalize(vNormal);
    if (uHasNormalMap == 1) {
        N = PerturbNormal(N, vFragPos, vTexCoord);
    }
    vec3 V = normalize(uCamPos - vFragPos);
    vec3 L = normalize(uSunDir);
    float NdotV = max(dot(N, V), 0.0);

    // Opt-in Wet Asphalt & Puddle Simulation (never leaks onto dry furniture/shoes)
    float isGround = step(abs(vFragPos.y), 0.65) * step(0.70, N.y);
    float effWet = (uAllowWetPuddles == 1) ? clamp(uWetness, 0.0, 1.0) : 0.0;
    float puddle = 0.0;
    float wetFringe = 0.0;
    if (effWet > 0.01 && isGround > 0.0) {
        float puddleScale = 0.38;
        if (uShaderQuality == 0) {
            float pn = (sin(vFragPos.x * puddleScale) * cos(vFragPos.z * (puddleScale * 1.28))) * 0.5 + 0.5;
            puddle = smoothstep(0.50, 0.68, pn) * effWet;
            wetFringe = smoothstep(0.40, 0.52, pn) * effWet;
        } else {
            float pn1 = sin(vFragPos.x * puddleScale) * cos(vFragPos.z * (puddleScale * 1.28));
            float pn2 = sin(vFragPos.x * (puddleScale * 2.1) + 1.8) * cos(vFragPos.z * (puddleScale * 1.7) + 2.9);
            float pn3 = sin(vFragPos.x * (puddleScale * 4.3) + 3.7) * cos(vFragPos.z * (puddleScale * 3.9) + 0.8);
            float puddleNoise = (pn1 * 0.50 + pn2 * 0.35 + pn3 * 0.15) * 0.5 + 0.5;
            puddle = smoothstep(0.48, 0.68, puddleNoise) * effWet;
            wetFringe = smoothstep(0.38, 0.52, puddleNoise) * effWet;
        }
    }

    albedo = mix(albedo, albedo * 0.35, wetFringe * 0.75 + puddle * 0.25);
    float roughness = mix(baseRoughness, 0.04, puddle);

    vec3 effN = N;
    if (puddle > 0.01 && uShaderQuality >= 1) {
        float ripple = (sin(vFragPos.x * 20.0 + uTime * 2.8) + cos(vFragPos.z * 20.0 + uTime * 2.2)) * 0.018 * puddle;
        effN = normalize(mix(N, vec3(ripple, 1.0, -ripple), puddle * 0.88));
    }

    vec3 F0 = mix(vec3(0.04), albedo, baseMetallic);

    vec3 H = normalize(V + L);
    float NDF = DistributionGGX(effN, H, roughness);
    float G = GeometrySmith(effN, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - baseMetallic);
    vec3 specular = (NDF * G * F) / (4.0 * max(dot(effN, V), 0.0) * max(dot(effN, L), 0.0) + 0.0001);
    float NdotL = max(dot(effN, L), 0.0);

    // 3-Cascade 3x3 PCF Directional Shadow Map Evaluation
    float shadowFactor = 1.0;
    if (uShadowMapEnabled == 1 && uShaderQuality >= 1) {
        int cascadeIdx = 0;
        vec4 fragPosLS = vFragPosLightSpace0;
        if (vViewDist > uCascadeSplits.x && uNumShadowCascades > 1) {
            cascadeIdx = 1;
            fragPosLS = vFragPosLightSpace1;
        }
        if (vViewDist > uCascadeSplits.y && uNumShadowCascades > 2) {
            cascadeIdx = 2;
            fragPosLS = vFragPosLightSpace2;
        }

        vec3 projCoords = fragPosLS.xyz / max(fragPosLS.w, 0.0001);
        projCoords = projCoords * 0.5 + 0.5;
        if (projCoords.x >= 0.01 && projCoords.x <= 0.99 &&
            projCoords.y >= 0.01 && projCoords.y <= 0.99 &&
            projCoords.z <= 0.998)
        {
            float cScale = (cascadeIdx == 0) ? 1.0 : ((cascadeIdx == 1) ? 2.2 : 4.2);
            float bias = max(0.0022 * (1.0 - max(dot(N, L), 0.0)), 0.0007) * cScale;
            vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
            float shadowAcc = 0.0;
            for (int sx = -1; sx <= 1; ++sx) {
                for (int sy = -1; sy <= 1; ++sy) {
                    float pcfDepth = texture(uShadowMap, vec3(projCoords.xy + vec2(sx, sy) * texelSize, float(cascadeIdx))).r;
                    shadowAcc += (projCoords.z - bias > pcfDepth) ? 0.42 : 1.0;
                }
            }
            shadowFactor = shadowAcc / 9.0;
        }
    }

    vec3 directLight = (kD * albedo / PI + specular) * uSunColor * NdotL * shadowFactor;

    vec3 pointLightAccum = vec3(0.0);
    for (int i = 0; i < uNumPointLights && i < MAX_POINT_LIGHTS; ++i) {
        vec3 pLightDir = uPointLightPos[i] - vFragPos;
        float pDistSq = dot(pLightDir, pLightDir);
        float rad = uPointLightRadius[i];
        if (pDistSq >= rad * rad) continue;

        float pDist = sqrt(pDistSq);
        vec3 pL = pLightDir / max(pDist, 0.001);
        float pNdotL = dot(effN, pL);
        if (pNdotL <= 0.0) continue;

        vec3 pH = normalize(V + pL);
        float pNormDist = pDist / rad;
        float pAtten = clamp(1.0 - pNormDist * pNormDist, 0.0, 1.0);
        pAtten = (pAtten * pAtten) / (1.0 + 0.15 * pDist);

        float pNDF = DistributionGGX(effN, pH, roughness);
        float pG = GeometrySmith(effN, V, pL, roughness);
        vec3 pF = fresnelSchlick(max(dot(pH, V), 0.0), F0);

        vec3 pkS = pF;
        vec3 pkD = (vec3(1.0) - pkS) * (1.0 - baseMetallic);
        vec3 pSpecular = (pNDF * pG * pF) / (4.0 * NdotV * pNdotL + 0.0001);
        pointLightAccum += (pkD * albedo / PI + pSpecular) * uPointLightCol[i] * pNdotL * pAtten;
    }

    vec3 ambient = uAmbientColor * albedo * baseAO;
    float groundBounce = max(-effN.y * 0.5 + 0.5, 0.0);
    vec3 bounceLight = uAmbientColor * 0.25 * groundBounce * albedo * baseAO;

    float vertMask = clamp(1.0 - abs(N.y) * 1.6, 0.0, 1.0);
    float rimFactor = pow(clamp(1.0 - NdotV, 0.0, 1.0), 3.5) * vertMask;
    vec3 rimLight = mix(uAmbientColor * 1.5, uSunColor, 0.4) * rimFactor * 0.30;

    vec3 puddleReflection = vec3(0.0);
    if (puddle > 0.01) {
        vec3 R = reflect(-V, effN);
        vec3 waterFresnel = fresnelSchlick(max(dot(effN, V), 0.0), vec3(0.04));
        vec3 skyReflect = mix(uFogColor * 1.3, vec3(0.02, 0.035, 0.075), clamp(R.y, 0.0, 1.0));
        skyReflect += uSunColor * pow(max(dot(R, L), 0.0), 180.0) * 3.5;

        if (uShaderQuality >= 1) {
            for (int i = 0; i < uNumPointLights && i < MAX_POINT_LIGHTS; ++i) {
                vec3 toPl = normalize(uPointLightPos[i] - vFragPos);
                float distPl = length(uPointLightPos[i] - vFragPos);
                float attenPl = clamp(1.0 - (distPl / uPointLightRadius[i]), 0.0, 1.0);
                skyReflect += uPointLightCol[i] * pow(max(dot(R, toPl), 0.0), 96.0) * 3.2 * attenPl;
            }
            float horizonGlow = smoothstep(0.0, 0.25, R.y) * smoothstep(0.75, 0.0, R.y);
            skyReflect += vec3(0.12, 0.08, 0.18) * horizonGlow;
        }
        puddleReflection = skyReflect * waterFresnel * puddle;
    }

    float beatBoost = 1.0 + 0.24 * clamp(uBeatPulse, 0.0, 1.0);
    vec3 emissiveLight = uEmissive * uGlowIntensity * beatBoost;
    if (uHasEmissiveMap == 1) {
        vec3 emTex = texture(uEmissiveMap, vTexCoord).rgb;
        vec3 emBase = (length(uEmissive) > 0.01) ? uEmissive : vec3(1.0);
        emissiveLight = emTex * emBase * uGlowIntensity * beatBoost;
    } else if (uHasTexture == 1 && length(uEmissive) > 0.01) {
        emissiveLight = texSample.rgb * uEmissive * 1.6 * uGlowIntensity * beatBoost;
    }

    vec3 color = ambient + directLight + pointLightAccum + bounceLight + rimLight + puddleReflection + emissiveLight;

    float dist = length(uCamPos - vFragPos);
    float fogFactor = smoothstep(0.0, 1.0, clamp((dist - 40.0) / 180.0, 0.0, 1.0));
    color = mix(color, uFogColor, fogFactor);

    if (uOutputLinearHDR == 0) {
        vec3 a = color * (2.51 * color + vec3(0.03));
        vec3 b = color * (2.43 * color + vec3(0.59)) + vec3(0.14);
        color = clamp(a / b, 0.0, 1.0);
        color = pow(color, vec3(1.0 / 2.2));
    }

    FragColor = vec4(color, (uAlphaMode == 2) ? alpha : 1.0);
}
)";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, pbrVS);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, pbrFS);
        if (!vs || !fs) return false;

        pbrProg = glCreateProgram();
        glAttachShader(pbrProg, vs);
        glAttachShader(pbrProg, fs);
        bool pbrLinked = LinkProgram(pbrProg, "pbrProg");
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!pbrLinked) { glDeleteProgram(pbrProg); pbrProg = 0; return false; }

        BindBoneBlock(pbrProg);
        uMVPLoc = glGetUniformLocation(pbrProg, "uMVP");
        uModelLoc = glGetUniformLocation(pbrProg, "uModel");
        uNormalMatLoc = glGetUniformLocation(pbrProg, "uNormalMatrix");
        uLightSpaceMatrixLoc = glGetUniformLocation(pbrProg, "uLightSpaceMatrix");
        if (uLightSpaceMatrixLoc == -1) uLightSpaceMatrixLoc = glGetUniformLocation(pbrProg, "uLightSpaceMatrix[0]");
        uCascadeSplitsLoc = glGetUniformLocation(pbrProg, "uCascadeSplits");
        uNumShadowCascadesLoc = glGetUniformLocation(pbrProg, "uNumShadowCascades");
        uShadowMapLoc = glGetUniformLocation(pbrProg, "uShadowMap");
        uShadowMapEnabledLoc = glGetUniformLocation(pbrProg, "uShadowMapEnabled");
        uBeatPulseLoc = glGetUniformLocation(pbrProg, "uBeatPulse");
        uSunDirLoc = glGetUniformLocation(pbrProg, "uSunDir");
        uSunColorLoc = glGetUniformLocation(pbrProg, "uSunColor");
        uAmbientColorLoc = glGetUniformLocation(pbrProg, "uAmbientColor");
        uCamPosLoc = glGetUniformLocation(pbrProg, "uCamPos");
        uAlbedoLoc = glGetUniformLocation(pbrProg, "uAlbedo");
        uMetallicLoc = glGetUniformLocation(pbrProg, "uMetallic");
        uRoughnessLoc = glGetUniformLocation(pbrProg, "uRoughness");
        uAmbientOcclusionLoc = glGetUniformLocation(pbrProg, "uAmbientOcclusion");
        uEmissiveLoc = glGetUniformLocation(pbrProg, "uEmissive");
        uTimeLoc = glGetUniformLocation(pbrProg, "uTime");
        uNumPointLightsLoc = glGetUniformLocation(pbrProg, "uNumPointLights");
        uPointLightPosLoc = glGetUniformLocation(pbrProg, "uPointLightPos");
        uPointLightColLoc = glGetUniformLocation(pbrProg, "uPointLightCol");
        uPointLightRadiusLoc = glGetUniformLocation(pbrProg, "uPointLightRadius");
        uAlbedoMapLoc = glGetUniformLocation(pbrProg, "uAlbedoMap");
        uNormalMapLoc = glGetUniformLocation(pbrProg, "uNormalMap");
        uMRMapLoc = glGetUniformLocation(pbrProg, "uMRMap");
        uAOMapLoc = glGetUniformLocation(pbrProg, "uAOMap");
        uEmissiveMapLoc = glGetUniformLocation(pbrProg, "uEmissiveMap");
        uHasTextureLoc = glGetUniformLocation(pbrProg, "uHasTexture");
        uHasNormalMapLoc = glGetUniformLocation(pbrProg, "uHasNormalMap");
        uHasMRMapLoc = glGetUniformLocation(pbrProg, "uHasMRMap");
        uHasAOMapLoc = glGetUniformLocation(pbrProg, "uHasAOMap");
        uHasEmissiveMapLoc = glGetUniformLocation(pbrProg, "uHasEmissiveMap");
        uAlphaModeLoc = glGetUniformLocation(pbrProg, "uAlphaMode");
        uAlphaCutoffLoc = glGetUniformLocation(pbrProg, "uAlphaCutoff");
        uAllowWetPuddlesLoc = glGetUniformLocation(pbrProg, "uAllowWetPuddles");
        uUseSkinningLoc = glGetUniformLocation(pbrProg, "uUseSkinning");
        uFootOffsetLoc = glGetUniformLocation(pbrProg, "uFootOffset");
        uOutputLinearHDRLoc = glGetUniformLocation(pbrProg, "uOutputLinearHDR");
        uFogColorLoc = glGetUniformLocation(pbrProg, "uFogColor");
        uWetnessLoc = glGetUniformLocation(pbrProg, "uWetness");
        uGlowIntensityLoc = glGetUniformLocation(pbrProg, "uGlowIntensity");
        uShaderQualityLoc = glGetUniformLocation(pbrProg, "uShaderQuality");

        // 3. Fullscreen Quad Blit Shader (Note: quadTexCoords in screen-top-left convention; FBO pass uses GL UV!)
        const char* qVS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
void main() {
    TexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";
        const char* qFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D uTexture;
void main() {
    FragColor = texture(uTexture, TexCoord);
}
)";
        GLuint qvs = CompileShader(GL_VERTEX_SHADER, qVS);
        GLuint qfs = CompileShader(GL_FRAGMENT_SHADER, qFS);
        if (!qvs || !qfs) return false;

        quadProg = glCreateProgram();
        glAttachShader(quadProg, qvs);
        glAttachShader(quadProg, qfs);
        bool quadLinked = LinkProgram(quadProg, "quadProg");
        glDeleteShader(qvs);
        glDeleteShader(qfs);
        if (!quadLinked) { glDeleteProgram(quadProg); quadProg = 0; return false; }

        // FBO post-process vertex shader uses standard bottom-up FBO texture coordinates (aPos * 0.5 + 0.5)
        const char* postVS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
out vec2 TexCoord;
void main() {
    TexCoord = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";
        const char* bloomExtractFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D uHdrTex;
uniform float uThreshold;
void main() {
    vec3 col = texture(uHdrTex, TexCoord).rgb;
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float soft = clamp((lum - uThreshold) / max(lum, 0.0001), 0.0, 1.0);
    FragColor = vec4(col * soft, 1.0);
}
)";
        const char* bloomBlurFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D uTex;
uniform int uHorizontal;
void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 texOffset = 1.0 / vec2(textureSize(uTex, 0));
    vec3 res = texture(uTex, TexCoord).rgb * w[0];
    vec2 dir = (uHorizontal == 1) ? vec2(texOffset.x, 0.0) : vec2(0.0, texOffset.y);
    for (int i = 1; i < 5; ++i) {
        res += texture(uTex, TexCoord + dir * float(i)).rgb * w[i];
        res += texture(uTex, TexCoord - dir * float(i)).rgb * w[i];
    }
    FragColor = vec4(res, 1.0);
}
)";
        const char* hdrCompositeFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D uHdrTex;
uniform sampler2D uBloomTex;
uniform float uBloomIntensity;
uniform float uExposure;
void main() {
    vec3 hdr = texture(uHdrTex, TexCoord).rgb + texture(uBloomTex, TexCoord).rgb * uBloomIntensity;
    hdr *= uExposure;
    vec3 a = hdr * (2.51 * hdr + vec3(0.03));
    vec3 b = hdr * (2.43 * hdr + vec3(0.59)) + vec3(0.14);
    vec3 col = clamp(a / b, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);
}
)";
        auto buildPost = [&](const char* fsCode, const char* name) -> GLuint {
            GLuint pvs = CompileShader(GL_VERTEX_SHADER, postVS);
            GLuint pfs = CompileShader(GL_FRAGMENT_SHADER, fsCode);
            if (!pvs || !pfs) { if (pvs) glDeleteShader(pvs); if (pfs) glDeleteShader(pfs); return 0; }
            GLuint p = glCreateProgram();
            glAttachShader(p, pvs);
            glAttachShader(p, pfs);
            bool ok = LinkProgram(p, name);
            glDeleteShader(pvs);
            glDeleteShader(pfs);
            if (!ok) { glDeleteProgram(p); return 0; }
            return p;
        };
        bloomExtractProg = buildPost(bloomExtractFS, "bloomExtractProg");
        if (bloomExtractProg) uBloomThresholdLoc = glGetUniformLocation(bloomExtractProg, "uThreshold");
        bloomBlurProg = buildPost(bloomBlurFS, "bloomBlurProg");
        if (bloomBlurProg) uBloomBlurHorizLoc = glGetUniformLocation(bloomBlurProg, "uHorizontal");
        hdrCompositeProg = buildPost(hdrCompositeFS, "hdrCompositeProg");
        if (hdrCompositeProg) {
            uCompositeHdrLoc = glGetUniformLocation(hdrCompositeProg, "uHdrTex");
            uCompositeBloomLoc = glGetUniformLocation(hdrCompositeProg, "uBloomTex");
            uCompositeBloomIntLoc = glGetUniformLocation(hdrCompositeProg, "uBloomIntensity");
            uCompositeExposureLoc = glGetUniformLocation(hdrCompositeProg, "uExposure");
        }

        // 4. Celestial Sky Dome Shader
        const char* skyVS = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
out vec2 vNDC;
void main() {
    vNDC = aPos;
    gl_Position = vec4(aPos, 0.99999, 1.0);
}
)";
        const char* skyFS = R"(
#version 330 core
out vec4 FragColor;
in vec2 vNDC;

uniform mat4 uInvViewProj;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uIsNight;
uniform int uOutputLinearHDR;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main() {
    vec4 clip = vec4(vNDC, 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    vec3 ray = normalize(world.xyz / world.w);

    vec3 sky;
    if (ray.y >= 0.0) {
        float t = pow(ray.y, 0.55);
        sky = mix(uHorizonColor, uZenithColor, t);

        vec3 sDir = normalize(uSunDir);
        float sunDot = max(dot(ray, sDir), 0.0);
        if (uIsNight > 0.5) {
            float moonDisc = pow(sunDot, 512.0) * 1.5;
            float moonGlow = pow(sunDot, 48.0) * 0.10;
            sky += (moonDisc + moonGlow) * uSunColor;
        } else {
            float sunDisc = pow(sunDot, 320.0) * 1.8;
            float sunGlow = pow(sunDot, 14.0) * 0.35;
            sky += (sunDisc + sunGlow) * uSunColor;
        }

        if (uIsNight > 0.5) {
            vec3 starGrid = floor(ray * 520.0);
            float h = hash(starGrid);
            if (h > 0.991) {
                float starBright = (h - 0.991) / 0.009;
                sky += vec3(starBright * 0.85, starBright * 0.92, starBright * 1.0);
            }
        }
    } else {
        float t = clamp(-ray.y * 3.5, 0.0, 1.0);
        sky = mix(uHorizonColor, uGroundColor, t);
    }

    if (uOutputLinearHDR == 0) {
        vec3 a = sky * (2.51 * sky + vec3(0.03));
        vec3 b = sky * (2.43 * sky + vec3(0.59)) + vec3(0.14);
        sky = clamp(a / b, 0.0, 1.0);
        sky = pow(sky, vec3(1.0 / 2.2));
    }

    FragColor = vec4(sky, 1.0);
}
)";
        GLuint svs = CompileShader(GL_VERTEX_SHADER, skyVS);
        GLuint sfs = CompileShader(GL_FRAGMENT_SHADER, skyFS);
        if (svs && sfs) {
            skyProg = glCreateProgram();
            glAttachShader(skyProg, svs);
            glAttachShader(skyProg, sfs);
            bool skyLinked = LinkProgram(skyProg, "skyProg");
            glDeleteShader(svs);
            glDeleteShader(sfs);
            if (!skyLinked) { glDeleteProgram(skyProg); skyProg = 0; return false; }

            uSkyInvVPLoc = glGetUniformLocation(skyProg, "uInvViewProj");
            uSkyZenithLoc = glGetUniformLocation(skyProg, "uZenithColor");
            uSkyHorizonLoc = glGetUniformLocation(skyProg, "uHorizonColor");
            uSkyGroundLoc = glGetUniformLocation(skyProg, "uGroundColor");
            uSkySunDirLoc = glGetUniformLocation(skyProg, "uSunDir");
            uSkySunColLoc = glGetUniformLocation(skyProg, "uSunColor");
            uSkyIsNightLoc = glGetUniformLocation(skyProg, "uIsNight");
            uSkyOutputLinearHDRLoc = glGetUniformLocation(skyProg, "uOutputLinearHDR");
        }

        return true;
    }

    GLuint CompileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);

        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << "[-] Shader compile error: " << log << "\n";
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    void SetupFullscreenQuad() {
        float quadVertices[] = {
            -1.0f,  1.0f,  0.0f, 0.0f,
            -1.0f, -1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 1.0f,

            -1.0f,  1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 1.0f,
             1.0f,  1.0f,  1.0f, 0.0f
        };

        glGenVertexArrays(1, &quadVAO);
        glBindVertexArray(quadVAO);

        glGenBuffers(1, &quadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    void SetupTextures() {
        glGenTextures(1, &screenTex);
        glBindTexture(GL_TEXTURE_2D, screenTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenWidth, screenHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenTextures(1, &hudTex);
        glBindTexture(GL_TEXTURE_2D, hudTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenWidth, screenHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    void SetupBoneUBO() {
        glGenBuffers(1, &boneUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, boneUBO);
        glBufferData(GL_UNIFORM_BUFFER, MAX_GPU_BONES * sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, boneUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void SetupShadowMap() {
        glGenFramebuffers(1, &shadowFBO);
        glGenTextures(1, &shadowDepthTex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowDepthTex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                     SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, NUM_SHADOW_CASCADES,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowDepthTex, 0, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &shadowFBO);
            glDeleteTextures(1, &shadowDepthTex);
            shadowFBO = 0;
            shadowDepthTex = 0;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SetupHDRPipeline() {
        glGenFramebuffers(1, &hdrFBO);
        glGenTextures(1, &hdrColorTex);
        glGenRenderbuffers(1, &hdrDepthRBO);
        glGenFramebuffers(2, bloomFBO);
        glGenTextures(2, bloomTex);
        ResizeHDRTargets();
    }

    void ResizeHDRTargets() {
        if (!hdrFBO || !hdrColorTex || !hdrDepthRBO) return;
        bloomWidth = std::max(1u, screenWidth / 2);
        bloomHeight = std::max(1u, screenHeight / 2);

        glBindTexture(GL_TEXTURE_2D, hdrColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, screenWidth, screenHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindRenderbuffer(GL_RENDERBUFFER, hdrDepthRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, screenWidth, screenHeight);

        glBindFramebuffer(GL_FRAMEBUFFER, hdrFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrColorTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hdrDepthRBO);
        bool hdrOk = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        bool bloomOk = true;
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, bloomTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bloomWidth, bloomHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            glBindFramebuffer(GL_FRAMEBUFFER, bloomFBO[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) bloomOk = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        hdrPipelineReady = hdrOk && bloomOk && (bloomExtractProg != 0) && (bloomBlurProg != 0) && (hdrCompositeProg != 0);
    }
};

} // namespace Surreal
