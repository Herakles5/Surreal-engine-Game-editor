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
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

namespace Surreal {

enum class RenderBackend {
    CPU_AVX2,     // Bare-Metal CosmOS Software SIMD Rasterizer
    GPU_OPENGL    // Linux Desktop OpenGL 4.6 Hardware Core Profile
};

struct GLMeshBuffer {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    size_t vertexCount;
    size_t indexCount;
};

class SurrealHybridRenderer {
public:
    RenderBackend backend;
    SDL_Window* window;
    SDL_GLContext glContext;
    bool isGLInitialized;

    uint32_t screenWidth;
    uint32_t screenHeight;

    // OpenGL Shaders & Resources
    GLuint pbrProg;
    GLuint quadProg;
    GLuint quadVAO, quadVBO;
    GLuint screenTex;
    GLuint hudTex;

    // Uniform Locations
    GLint uMVPLoc;
    GLint uModelLoc;
    GLint uSunDirLoc;
    GLint uSunColorLoc;
    GLint uAmbientColorLoc;
    GLint uCamPosLoc;
    GLint uAlbedoLoc;
    GLint uMetallicLoc;
    GLint uRoughnessLoc;
    GLint uAlbedoMapLoc;
    GLint uHasTextureLoc;

    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;

    std::unordered_map<const SurrealMesh*, GLMeshBuffer> meshBuffers;

    SurrealHybridRenderer()
        : backend(RenderBackend::GPU_OPENGL),
          window(nullptr),
          glContext(nullptr),
          isGLInitialized(false),
          screenWidth(960),
          screenHeight(640),
          pbrProg(0), quadProg(0),
          quadVAO(0), quadVBO(0),
          screenTex(0), hudTex(0),
          uMVPLoc(-1), uModelLoc(-1),
          uSunDirLoc(-1), uSunColorLoc(-1),
          uAmbientColorLoc(-1), uCamPosLoc(-1),
          uAlbedoLoc(-1), uMetallicLoc(-1),
          uRoughnessLoc(-1) {}

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
        SDL_GL_SetSwapInterval(1); // VSync

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

        isGLInitialized = true;
        return true;
    }

    /**
     * Resize GL viewport + screen/HUD textures (window resize / fullscreen toggle).
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
            glViewport(0, 0, screenWidth, screenHeight);
            glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDisable(GL_CULL_FACE); // Double sided rendering

            glUseProgram(pbrProg);
        }
    }

    void SetGlobalUniforms(const Vec3& sunDir, const ColorRGB& sunColor, const ColorRGB& ambientColor, const Vec3& camPos) {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized) return;

        glUseProgram(pbrProg);
        glUniform3f(uSunDirLoc, sunDir.x, sunDir.y, sunDir.z);
        glUniform3f(uSunColorLoc, sunColor.r, sunColor.g, sunColor.b);
        glUniform3f(uAmbientColorLoc, ambientColor.r, ambientColor.g, ambientColor.b);
        glUniform3f(uCamPosLoc, camPos.x, camPos.y, camPos.z);
    }

    void RenderMesh(
        const std::shared_ptr<SurrealMesh>& mesh,
        const Mat4& worldMat,
        const Mat4& vpMat,
        const PBRMaterial& material,
        bool isDynamic = false)
    {
        if (backend != RenderBackend::GPU_OPENGL || !isGLInitialized || !mesh) return;

        const SurrealMesh* meshKey = mesh.get();
        auto it = meshBuffers.find(meshKey);

        if (it == meshBuffers.end()) {
            GLMeshBuffer buf;
            buf.vertexCount = mesh->vertices.size();
            buf.indexCount = mesh->indices.size();

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

            // Attribute 0: Position (3 floats at offset 0)
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));
            glEnableVertexAttribArray(0);

            // Attribute 1: Normal (3 floats at offset 16)
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));
            glEnableVertexAttribArray(1);

            // Attribute 2: UVs (2 floats at offset 32)
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, u));
            glEnableVertexAttribArray(2);

            // Attribute 3: Vertex Color (4 floats at offset 48)
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, color));
            glEnableVertexAttribArray(3);

            meshBuffers[meshKey] = buf;
            it = meshBuffers.find(meshKey);
        } else if (isDynamic) {
            // Update vertex buffer for skeletal deformation
            glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, mesh->vertices.size() * sizeof(Vertex3D), mesh->vertices.data());
        }

        glUseProgram(pbrProg);

        Mat4 mvpMat = vpMat * worldMat;
        glUniformMatrix4fv(uMVPLoc, 1, GL_TRUE, &mvpMat.m[0][0]);
        glUniformMatrix4fv(uModelLoc, 1, GL_TRUE, &worldMat.m[0][0]);

        glBindVertexArray(it->second.vao);

        if (mesh->submeshes.empty()) {
            glUniform1i(uHasTextureLoc, 0);
            glUniform3f(uAlbedoLoc, material.albedo.r, material.albedo.g, material.albedo.b);
            glUniform1f(uMetallicLoc, material.metallic);
            glUniform1f(uRoughnessLoc, material.roughness);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(it->second.indexCount), GL_UNSIGNED_INT, 0);
        } else {
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(uAlbedoMapLoc, 0);

            for (auto& sub : mesh->submeshes) {
                if (sub.indexCount == 0) continue;

                // Lazy GPU texture upload
                if (sub.textureId == 0 && sub.texRGBA && !sub.texRGBA->empty()) {
                    glGenTextures(1, &sub.textureId);
                    glBindTexture(GL_TEXTURE_2D, sub.textureId);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sub.texWidth, sub.texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sub.texRGBA->data());
                    glGenerateMipmap(GL_TEXTURE_2D);
                }

                if (sub.textureId != 0) {
                    glBindTexture(GL_TEXTURE_2D, sub.textureId);
                    glUniform1i(uHasTextureLoc, 1);
                } else {
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glUniform1i(uHasTextureLoc, 0);
                }

                ColorRGB effectiveAlbedo = sub.albedo * material.albedo;
                glUniform3f(uAlbedoLoc, effectiveAlbedo.r, effectiveAlbedo.g, effectiveAlbedo.b);
                glUniform1f(uMetallicLoc, sub.metallic);
                glUniform1f(uRoughnessLoc, sub.roughness);

                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(sub.indexCount), GL_UNSIGNED_INT,
                               reinterpret_cast<const void*>(static_cast<uintptr_t>(sub.indexOffset * sizeof(uint32_t))));
            }
        }

        glBindVertexArray(0);
    }

    void BlitSoftwareFramebuffer(const uint32_t* fb, int w, int h) {
        if (!isGLInitialized || !fb) return;

        glViewport(0, 0, screenWidth, screenHeight);
        glBindTexture(GL_TEXTURE_2D, screenTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, fb);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(quadProg);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void BlitHUDOverlay(const uint32_t* hudFb, int w, int h) {
        if (!isGLInitialized || !hudFb) return;

        glBindTexture(GL_TEXTURE_2D, hudTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, hudFb);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(quadProg);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    void Present() {
        if (isGLInitialized && window) {
            SDL_GL_SwapWindow(window);
        }
    }

    void Cleanup() {
        for (auto& pair : meshBuffers) {
            glDeleteBuffers(1, &pair.second.vbo);
            glDeleteBuffers(1, &pair.second.ebo);
            glDeleteVertexArrays(1, &pair.second.vao);
        }
        meshBuffers.clear();

        if (screenTex) glDeleteTextures(1, &screenTex);
        if (hudTex) glDeleteTextures(1, &hudTex);
        if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
        if (quadVBO) glDeleteBuffers(1, &quadVBO);
        if (pbrProg) glDeleteProgram(pbrProg);
        if (quadProg) glDeleteProgram(quadProg);

        if (glContext) {
            SDL_GL_DeleteContext(glContext);
            glContext = nullptr;
        }
        isGLInitialized = false;
    }

private:
    bool BuildShaders() {
        // 1. Cook-Torrance PBR Vertex Shader
        const char* pbrVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec4 aColor;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vColor;

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

        // 2. Cook-Torrance PBR Fragment Shader
        const char* pbrFS = R"(
#version 330 core
out vec4 FragColor;

in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uCamPos;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform sampler2D uAlbedoMap;
uniform int uHasTexture;

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
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    float alpha = vColor.a;
    vec3 baseAlbedo = uAlbedo * vColor.rgb;
    if (uHasTexture == 1) {
        vec4 texSample = texture(uAlbedoMap, vTexCoord);
        if (texSample.a < 0.15) discard; // Alpha cutout for foliage, leaves & grates
        baseAlbedo *= texSample.rgb;
        alpha *= texSample.a;
    }
    vec3 albedo = baseAlbedo;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vFragPos);
    vec3 L = normalize(uSunDir);
    vec3 H = normalize(V + L);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, uMetallic);

    float NDF = DistributionGGX(N, H, uRoughness);
    float G = GeometrySmith(N, V, L, uRoughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - uMetallic;

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    vec3 directLight = (kD * albedo / PI + specular) * uSunColor * NdotL;
    vec3 ambient = uAmbientColor * albedo;

    // 1. Floor Ambient Bounce (prevents pitch-black under-surfaces)
    float groundBounce = max(-N.y * 0.5 + 0.5, 0.0);
    vec3 bounceLight = vec3(0.08, 0.07, 0.12) * groundBounce * albedo;

    // 2. Fresnel Rim Lighting (vibrant cyberpunk silhouette glow on character & geometry edges)
    float NdotV = max(dot(N, V), 0.0);
    float rimFactor = pow(clamp(1.0 - NdotV, 0.0, 1.0), 3.2);
    vec3 rimLight = vec3(0.35, 0.65, 1.0) * rimFactor * 0.45;

    vec3 color = ambient + directLight + bounceLight + rimLight;

    // 3. Atmospheric Distance Depth Fog (Extended range for expansive metropolis view)
    float dist = length(uCamPos - vFragPos);
    float fogFactor = clamp((dist - 25.0) / 160.0, 0.0, 1.0);
    fogFactor = smoothstep(0.0, 1.0, fogFactor);
    vec3 fogColor = uAmbientColor * 0.55 + vec3(0.015, 0.02, 0.04);
    color = mix(color, fogColor, fogFactor);

    // 4. ACES Filmic Tone Mapping (professional cinematic contrast)
    vec3 a = color * (2.51 * color + vec3(0.03));
    vec3 b = color * (2.43 * color + vec3(0.59)) + vec3(0.14);
    color = clamp(a / b, 0.0, 1.0);

    // Gamma Correction (sRGB standard)
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, alpha);
}
)";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, pbrVS);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, pbrFS);
        if (!vs || !fs) return false;

        pbrProg = glCreateProgram();
        glAttachShader(pbrProg, vs);
        glAttachShader(pbrProg, fs);
        glLinkProgram(pbrProg);

        glDeleteShader(vs);
        glDeleteShader(fs);

        uMVPLoc = glGetUniformLocation(pbrProg, "uMVP");
        uModelLoc = glGetUniformLocation(pbrProg, "uModel");
        uSunDirLoc = glGetUniformLocation(pbrProg, "uSunDir");
        uSunColorLoc = glGetUniformLocation(pbrProg, "uSunColor");
        uAmbientColorLoc = glGetUniformLocation(pbrProg, "uAmbientColor");
        uCamPosLoc = glGetUniformLocation(pbrProg, "uCamPos");
        uAlbedoLoc = glGetUniformLocation(pbrProg, "uAlbedo");
        uMetallicLoc = glGetUniformLocation(pbrProg, "uMetallic");
        uRoughnessLoc = glGetUniformLocation(pbrProg, "uRoughness");
        uAlbedoMapLoc = glGetUniformLocation(pbrProg, "uAlbedoMap");
        uHasTextureLoc = glGetUniformLocation(pbrProg, "uHasTexture");

        // 3. Fullscreen Quad Blit Shader
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
    vec4 col = texture(uTexture, TexCoord);
    if (col.a < 0.01) discard;
    FragColor = col;
}
)";

        GLuint qvs = CompileShader(GL_VERTEX_SHADER, qVS);
        GLuint qfs = CompileShader(GL_FRAGMENT_SHADER, qFS);
        if (!qvs || !qfs) return false;

        quadProg = glCreateProgram();
        glAttachShader(quadProg, qvs);
        glAttachShader(quadProg, qfs);
        glLinkProgram(quadProg);

        glDeleteShader(qvs);
        glDeleteShader(qfs);

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
        // Quad vertices (Position X, Y, TexCoord U, V)
        // Flip V so (0,0) in framebuffer top-left renders top-left
        float quadVertices[] = {
            // Pos         // UV
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
        // Screen Texture
        glGenTextures(1, &screenTex);
        glBindTexture(GL_TEXTURE_2D, screenTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenWidth, screenHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // HUD Texture
        glGenTextures(1, &hudTex);
        glBindTexture(GL_TEXTURE_2D, hudTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenWidth, screenHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
};

} // namespace Surreal
