/**
 * ============================================================================
 * SMASH CATS 3D: SURREAL EDITION - LINUX & BARE-METAL HYBRID GAME BINARY
 *
 * First flagship playable 3D action game built natively on the C++20 Surreal
 * Engine (OpenGL 4.6 GPU + 3-Cascade CSM Shadows + HDR Bloom + GPU Skinning,
 * hot-swappable to AVX2 CPU Software Rasterizer via [F9], plus SDL2 Audio).
 *
 * Build: make surreal-smash-cats
 * Run:   ./surreal-smash-cats [--headless] [--arena seoul|cyber] [--fullscreen]
 * ============================================================================
 */

#include <SDL2/SDL.h>
#include "surreal_smash_cats.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <omp.h>

namespace {

/**
 * Lightweight polyphonic procedural SDL2 cyber-synth for zero-asset SFX.
 */
class SmashAudioSynth {
public:
    struct Voice {
        bool active = false;
        float freq = 220.0f;
        float freqEnd = 110.0f;
        float age = 0.0f;
        float duration = 0.15f;
        float gain = 0.25f;
        float phase = 0.0f;
        bool noiseMix = false;
    };

    SDL_AudioDeviceID dev = 0;
    std::array<Voice, 12> voices{};
    uint32_t rng = 0x13579BDFu;
    int sampleRate = 44100;

    bool Init() {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        SDL_AudioSpec want = {}, have = {};
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = AudioCallback;
        want.userdata = this;
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (dev == 0) return false;
        sampleRate = (have.freq > 0) ? have.freq : 44100;
        SDL_PauseAudioDevice(dev, 0);
        return true;
    }

    void Shutdown() {
        if (dev != 0) {
            SDL_CloseAudioDevice(dev);
            dev = 0;
        }
    }

    void Trigger(Surreal::SmashSFX sfx) {
        if (dev == 0) return;
        Voice v;
        v.active = true;
        switch (sfx) {
            case Surreal::SmashSFX::PUNCH_LIGHT: v.freq = 420.0f; v.freqEnd = 170.0f; v.duration = 0.09f; v.gain = 0.18f; v.noiseMix = true; break;
            case Surreal::SmashSFX::PUNCH_HEAVY: v.freq = 280.0f; v.freqEnd = 85.0f;  v.duration = 0.18f; v.gain = 0.28f; v.noiseMix = true; break;
            case Surreal::SmashSFX::HIT_IMPACT:  v.freq = 560.0f; v.freqEnd = 140.0f; v.duration = 0.11f; v.gain = 0.26f; v.noiseMix = true; break;
            case Surreal::SmashSFX::JUMP:        v.freq = 240.0f; v.freqEnd = 520.0f; v.duration = 0.14f; v.gain = 0.20f; break;
            case Surreal::SmashSFX::FLIP_KICK:   v.freq = 360.0f; v.freqEnd = 820.0f; v.duration = 0.22f; v.gain = 0.24f; v.noiseMix = true; break;
            case Surreal::SmashSFX::GROUND_SLAM: v.freq = 160.0f; v.freqEnd = 38.0f;  v.duration = 0.36f; v.gain = 0.42f; v.noiseMix = true; break;
            case Surreal::SmashSFX::PICKUP_ORB:  v.freq = 587.0f; v.freqEnd = 1174.0f;v.duration = 0.19f; v.gain = 0.22f; break;
            case Surreal::SmashSFX::PLAYER_HURT: v.freq = 190.0f; v.freqEnd = 90.0f;  v.duration = 0.22f; v.gain = 0.30f; v.noiseMix = true; break;
            case Surreal::SmashSFX::WAVE_CLEAR:  v.freq = 440.0f; v.freqEnd = 880.0f; v.duration = 0.32f; v.gain = 0.24f; break;
            case Surreal::SmashSFX::BOSS_SPAWN:  v.freq = 135.0f; v.freqEnd = 52.0f;  v.duration = 0.55f; v.gain = 0.36f; v.noiseMix = true; break;
            case Surreal::SmashSFX::GAME_OVER:   v.freq = 300.0f; v.freqEnd = 70.0f;  v.duration = 0.65f; v.gain = 0.34f; break;
        }
        SDL_LockAudioDevice(dev);
        for (auto& slot : voices) {
            if (!slot.active) { slot = v; break; }
        }
        SDL_UnlockAudioDevice(dev);
    }

private:
    static void SDLCALL AudioCallback(void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<SmashAudioSynth*>(userdata);
        auto* out = reinterpret_cast<int16_t*>(stream);
        const int count = len / static_cast<int>(sizeof(int16_t));
        const float dt = 1.0f / static_cast<float>(self->sampleRate);

        for (int i = 0; i < count; ++i) {
            float mix = 0.0f;
            for (auto& v : self->voices) {
                if (!v.active) continue;
                float t = v.age / std::max(0.001f, v.duration);
                if (t >= 1.0f) { v.active = false; continue; }
                float f = v.freq + (v.freqEnd - v.freq) * t;
                v.phase += f * dt * 6.2831853f;
                if (v.phase > 6.2831853f) v.phase -= 6.2831853f;

                float sig = sinf(v.phase);
                if (v.noiseMix) {
                    self->rng = self->rng * 1664525u + 1013904223u;
                    float n = (static_cast<float>(self->rng & 0xFFFFu) / 32768.0f) - 1.0f;
                    sig = sig * 0.65f + n * 0.35f;
                }
                float env = (1.0f - t) * (1.0f - t);
                mix += sig * v.gain * env;
                v.age += dt;
            }
            mix = std::max(-0.92f, std::min(0.92f, mix));
            out[i] = static_cast<int16_t>(mix * 32767.0f);
        }
    }
};

void SetupRuntimeEnvironment(char* argv[]) {
    const char* ompPolicy = std::getenv("OMP_WAIT_POLICY");
    const char* gompSpin = std::getenv("GOMP_SPINCOUNT");
    if (!ompPolicy || std::string(ompPolicy) != "PASSIVE" || !gompSpin || std::string(gompSpin) != "0") {
        setenv("OMP_WAIT_POLICY", "PASSIVE", 1);
        setenv("GOMP_SPINCOUNT", "0", 1);
        execv("/proc/self/exe", argv);
    }
    omp_set_num_threads(std::min(4, std::max(2, omp_get_num_procs() / 2)));

    if (access("/dev/nvidia0", R_OK) == 0 && access("/sys/bus/pci/drivers/nvidia/0000:01:00.0", F_OK) == 0) {
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
        setenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER", "NVIDIA-G0", 0);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
        setenv("__VK_LAYER_NV_optimus", "NVIDIA_only", 0);
    }
}

int RunHeadlessSelfTest(bool seoulArena, Surreal::SmashCatClass startClass) {
    constexpr uint32_t W = 640, H = 400;
    Surreal::SmashCatsGame game(W, H);
    game.Init(seoulArena, true);
    if (startClass != Surreal::SmashCatClass::KITTEN_MONK) {
        game.SwitchClass(startClass, true);
    }

    int loadedModels = 0;
    for (int i = 0; i < static_cast<int>(Surreal::SmashEnemyKind::COUNT); ++i) {
        if (game.archetypes[i].loaded) loadedModels++;
    }

    std::vector<uint32_t> fb(W * H, 0xFF050812);
    game.engine.AttachFramebuffer(fb.data(), W * sizeof(uint32_t));

    // Exercise movement, jump, somersault, slam & attacks in CPU mode
    Surreal::CameraInputState camIn;
    game.TriggerJumpOrAcrobatics();
    for (int f = 0; f < 5; ++f) game.Update(0.016f, Surreal::Vec3(0, 0, -1), camIn, true);
    game.TriggerJumpOrAcrobatics(); // Flip
    game.Update(0.016f, Surreal::Vec3(0, 0, 0), camIn, true);
    game.TriggerGroundSlamDirect();
    for (int f = 0; f < 10; ++f) game.Update(0.016f, Surreal::Vec3(0, 0, 0), camIn, true);
    game.TriggerAttack(false);
    game.RenderSceneCPU(0.016f, fb.data(), W, H, 60.0f, "CPU AVX2 HEADLESS");

    int nonBgPixels = 0;
    for (uint32_t px : fb) if (px != 0xFF050812) nonBgPixels++;

    std::cout << "[SELF-TEST] Loaded 3D GLB archetypes: " << loadedModels << "/5"
              << " | Wave=" << game.wave
              << " | Enemies=" << game.CountAliveEnemies()
              << " | Score=" << game.score
              << " | Pixels=" << nonBgPixels << "\n";

    return (loadedModels >= 3 && (game.CountAliveEnemies() + static_cast<int>(game.spawnQueue.size())) >= 3 && nonBgPixels > 10000) ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    SetupRuntimeEnvironment(argv);

    bool headless = false;
    bool seoulArena = false;
    bool startFullscreen = false;
    bool startCpuBackend = false;
    Surreal::SmashCatClass startClass = Surreal::SmashCatClass::KITTEN_MONK;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless" || arg == "--self-test") headless = true;
        else if (arg == "--fullscreen") startFullscreen = true;
        else if (arg == "--cpu") startCpuBackend = true;
        else if (arg == "--arena" && i + 1 < argc) {
            std::string a = argv[++i];
            if (a == "seoul" || a == "city") seoulArena = true;
        } else if (arg == "--class" && i + 1 < argc) {
            std::string c = argv[++i];
            if (c == "cyber" || c == "cybercat") startClass = Surreal::SmashCatClass::CYBER_CAT;
            else if (c == "lioness") startClass = Surreal::SmashCatClass::LIONESS;
            else if (c == "monk" || c == "kitten") startClass = Surreal::SmashCatClass::KITTEN_MONK;
        }
    }

    std::cout << "=========================================================\n";
    std::cout << "  SMASH CATS 3D: SURREAL EDITION (C++20 / Surreal Engine)\n";
    std::cout << "  Playable on Linux Desktop & MeinOS Hybrid Platform\n";
    std::cout << "=========================================================\n";

    if (headless) {
        return RunHeadlessSelfTest(seoulArena, startClass);
    }

    int winW = 1280;
    int winH = 720;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "[-] SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "SMASH CATS 3D: Surreal Edition",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW, winH,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
    );
    if (!window) {
        std::cerr << "[-] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }
    if (startFullscreen) SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    Surreal::SurrealHybridRenderer hybridRenderer;
    hybridRenderer.Init(window, winW, winH);
    if (startCpuBackend && hybridRenderer.backend == Surreal::RenderBackend::GPU_OPENGL) {
        hybridRenderer.ToggleBackend();
    }

    SmashAudioSynth audio;
    audio.Init();

    std::vector<uint32_t> framebuffer(static_cast<size_t>(winW) * winH, 0xFF050812);
    std::vector<uint32_t> hudFramebuffer(static_cast<size_t>(winW) * winH, 0x00000000);

    const bool isCpuMode = (hybridRenderer.backend != Surreal::RenderBackend::GPU_OPENGL || !hybridRenderer.isGLInitialized);
    Surreal::SmashCatsGame game(winW, winH);
    game.engine.AttachFramebuffer(framebuffer.data(), winW * sizeof(uint32_t));
    game.onSFX = [&](Surreal::SmashSFX sfx) { audio.Trigger(sfx); };
    game.Init(seoulArena, isCpuMode);
    if (startClass != Surreal::SmashCatClass::KITTEN_MONK) {
        game.SwitchClass(startClass, isCpuMode);
    }

    std::cout << "[+] Ready! Controls:\n"
              << "    [WASD / Arrows] Move   | [SPACE x1/2/3] Jump / Flip / Ground Slam\n"
              << "    [LMB / J] Attack Combo | [RMB tap / F / K] Heavy Spin Kick\n"
              << "    [RMB Drag / Q / E] Rotate Camera | [Wheel] Zoom\n"
              << "    [1/2/3] Switch Cat (Kitten Monk / CyberCat / Lioness)\n"
              << "    [C] Fur Tint | [V] Seoul PBR Arena | [N] Sky | [F9] GPU/CPU\n" << std::flush;

    bool keyFwd = false, keyBack = false, keyLeft = false, keyRight = false;
    bool camLeft = false, camRight = false;
    bool rmbDown = false;
    int rmbDragPx = 0;

    Surreal::CameraInputState camInput;
    bool running = true;
    float currentFps = 60.0f;
    uint64_t frameCount = 0;
    uint64_t totalFrames = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();
    auto fpsStart = lastTime;

    while (running) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        dt = std::clamp(dt, 0.0005f, 0.05f);

        camInput.mouseDeltaX = 0.0f;
        camInput.mouseDeltaY = 0.0f;
        camInput.scrollDelta = 0.0f;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            const bool cpuActive = (hybridRenderer.backend != Surreal::RenderBackend::GPU_OPENGL || !hybridRenderer.isGLInitialized);
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode sym = ev.key.keysym.sym;
                uint16_t mod = ev.key.keysym.mod;
                if (ev.key.repeat == 0) {
                    if (sym == SDLK_ESCAPE) running = false;
                    else if (sym == SDLK_w || sym == SDLK_UP) keyFwd = true;
                    else if (sym == SDLK_s || sym == SDLK_DOWN) keyBack = true;
                    else if (sym == SDLK_a || sym == SDLK_LEFT) keyLeft = true;
                    else if (sym == SDLK_d || sym == SDLK_RIGHT) keyRight = true;
                    else if (sym == SDLK_q) camLeft = true;
                    else if (sym == SDLK_e) camRight = true;
                    else if (sym == SDLK_SPACE) game.TriggerJumpOrAcrobatics();
                    else if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT || sym == SDLK_x) game.TriggerGroundSlamDirect();
                    else if (sym == SDLK_j) game.TriggerAttack(false);
                    else if (sym == SDLK_k || sym == SDLK_f) game.TriggerAttack(true);
                    else if (sym == SDLK_1) game.SwitchClass(Surreal::SmashCatClass::KITTEN_MONK, cpuActive);
                    else if (sym == SDLK_2) game.SwitchClass(Surreal::SmashCatClass::CYBER_CAT, cpuActive);
                    else if (sym == SDLK_3) game.SwitchClass(Surreal::SmashCatClass::LIONESS, cpuActive);
                    else if (sym == SDLK_c) {
                        game.furPresetIdx = (game.furPresetIdx + 1) % 6;
                        game.SetBanner(std::string("FUR STYLE: ") + Surreal::kFurPresetNames[game.furPresetIdx], 2.0f);
                    } else if (sym == SDLK_v) {
                        game.ToggleArena();
                    } else if (sym == SDLK_n) {
                        game.ApplySkyProfile(game.skyProfileIdx + 1);
                        game.SetBanner(std::string("SKY: ") + game.skyProfiles[game.skyProfileIdx].name, 2.2f);
                    } else if (sym == SDLK_m) {
                        game.audioMuted = !game.audioMuted;
                        game.SetBanner(game.audioMuted ? "AUDIO: MUTED [M]" : "AUDIO: ACTIVE [M]", 1.8f);
                    } else if (sym == SDLK_p) {
                        if (!game.isGameOver) game.isPaused = !game.isPaused;
                    } else if (sym == SDLK_r || (sym == SDLK_RETURN && game.isGameOver)) {
                        game.ResetRun(cpuActive);
                    } else if (sym == SDLK_F9) {
                        hybridRenderer.ToggleBackend();
                        game.SetBanner("BACKEND: " + hybridRenderer.GetBackendName(), 2.5f);
                    } else if (sym == SDLK_F11 || (sym == SDLK_RETURN && (mod & KMOD_ALT))) {
                        bool isFs = (SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
                        SDL_SetWindowFullscreen(window, isFs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    }
                }
            } else if (ev.type == SDL_KEYUP) {
                SDL_Keycode sym = ev.key.keysym.sym;
                if (sym == SDLK_w || sym == SDLK_UP) keyFwd = false;
                else if (sym == SDLK_s || sym == SDLK_DOWN) keyBack = false;
                else if (sym == SDLK_a || sym == SDLK_LEFT) keyLeft = false;
                else if (sym == SDLK_d || sym == SDLK_RIGHT) keyRight = false;
                else if (sym == SDLK_q) camLeft = false;
                else if (sym == SDLK_e) camRight = false;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (game.isGameOver) game.ResetRun(cpuActive);
                    else game.TriggerAttack(false);
                } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                    rmbDown = true;
                    rmbDragPx = 0;
                    camInput.isRightMouseDown = true;
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP) {
                if (ev.button.button == SDL_BUTTON_RIGHT) {
                    if (rmbDown && rmbDragPx < 6) {
                        game.TriggerAttack(true); // Quick tap triggers Heavy Spin Kick
                    }
                    rmbDown = false;
                    camInput.isRightMouseDown = false;
                }
            } else if (ev.type == SDL_MOUSEMOTION) {
                if (rmbDown) {
                    rmbDragPx += std::abs(ev.motion.xrel) + std::abs(ev.motion.yrel);
                    camInput.mouseDeltaX += static_cast<float>(ev.motion.xrel);
                    camInput.mouseDeltaY += static_cast<float>(ev.motion.yrel);
                }
            } else if (ev.type == SDL_MOUSEWHEEL) {
                camInput.scrollDelta += static_cast<float>(ev.wheel.y);
            }
        }

        // Handle resize cleanly
        int dw = 0, dh = 0;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        if (dw > 0 && dh > 0 && (dw != winW || dh != winH)) {
            winW = dw;
            winH = dh;
            framebuffer.assign(static_cast<size_t>(winW) * winH, 0xFF050812);
            hudFramebuffer.assign(static_cast<size_t>(winW) * winH, 0x00000000);
            game.engine.Resize(winW, winH);
            game.engine.AttachFramebuffer(framebuffer.data(), winW * sizeof(uint32_t));
            hybridRenderer.Resize(winW, winH);
        }

        if (camLeft)  camInput.mouseDeltaX -= 760.0f * dt;
        if (camRight) camInput.mouseDeltaX += 760.0f * dt;

        // Camera-relative world movement direction
        Surreal::Vec3 camFwd(-sinf(game.camera.yaw), 0.0f, -cosf(game.camera.yaw));
        camFwd = camFwd.Normalized();
        Surreal::Vec3 camRightVec = camFwd.Cross(Surreal::Vec3(0, 1.0f, 0)).Normalized();
        Surreal::Vec3 wishMove(0, 0, 0);
        if (keyFwd)   wishMove = wishMove + camFwd;
        if (keyBack)  wishMove = wishMove - camFwd;
        if (keyRight) wishMove = wishMove + camRightVec;
        if (keyLeft)  wishMove = wishMove - camRightVec;

        const bool useCpuMode = (hybridRenderer.backend != Surreal::RenderBackend::GPU_OPENGL || !hybridRenderer.isGLInitialized);
        game.Update(dt, wishMove, camInput, useCpuMode);

        if (!useCpuMode) {
            bool refreshHud = (totalFrames % 2 == 0) || game.isPaused || game.isGameOver;
            game.RenderSceneGPU(hybridRenderer, winW, winH, true, hudFramebuffer.data(), refreshHud, currentFps);
        } else {
            game.RenderSceneCPU(dt, framebuffer.data(), winW, winH, currentFps, hybridRenderer.GetBackendName());
            hybridRenderer.BlitSoftwareFramebuffer(framebuffer.data(), winW, winH);
            hybridRenderer.Present();
        }

        frameCount++;
        totalFrames++;
        double fpsElapsed = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - fpsStart).count();
        if (fpsElapsed >= 0.4) {
            currentFps = static_cast<float>(frameCount / fpsElapsed);
            frameCount = 0;
            fpsStart = std::chrono::high_resolution_clock::now();
        }

        // Precision 60 FPS frame-pacer: prevents busy-loop CPU burn under X11 PRIME
        constexpr auto TARGET_FRAME_DUR = std::chrono::microseconds(16400);
        auto frameElapsed = std::chrono::high_resolution_clock::now() - now;
        if (frameElapsed < TARGET_FRAME_DUR) {
            std::this_thread::sleep_for(TARGET_FRAME_DUR - frameElapsed);
        }
    }

    game.SaveHighScore();
    audio.Shutdown();
    hybridRenderer.Cleanup();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
