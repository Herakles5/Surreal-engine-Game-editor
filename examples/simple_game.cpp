/**
 * ============================================================================
 * SURREAL ENGINE - MINIMAL STANDALONE GAME EXAMPLE
 * Demonstrates how external developers build 3D games with Surreal Engine
 * Compile: g++ -O3 -mavx2 simple_game.cpp $(pkg-config --cflags --libs surreal) -o simple_game
 * ============================================================================
 */

#include <surreal/surreal_engine.hpp>
#include <surreal/surreal_camera.hpp>
#include <surreal/surreal_terrain.hpp>
#include <surreal/surreal_hud.hpp>
#include <SDL2/SDL.h>
#include <iostream>

int main() {
    std::cout << "[+] Starting Minimal Surreal Game...\n";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    constexpr int W = 800;
    constexpr int H = 600;

    SDL_Window* window = SDL_CreateWindow(
        "Surreal Engine - Standalone Linux Game",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H, SDL_WINDOW_SHOWN
    );
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);

    std::vector<uint32_t> fb(W * H, 0xFF050812);
    Surreal::SurrealEngine engine(W, H);
    engine.AttachFramebuffer(fb.data(), W * sizeof(uint32_t));

    // 1. Create Ground Floor
    auto floor = engine.world.CreateEntity("Floor");
    Surreal::MeshRendererComponent mrFloor;
    mrFloor.mesh = Surreal::SurrealTerrainBuilder::CreateCyberGridFloor(20.0f, 20);
    mrFloor.material.albedo = Surreal::ColorRGB(0.8f, 0.8f, 1.0f);
    mrFloor.material.metallic = 0.2f;
    mrFloor.material.roughness = 0.5f;
    engine.world.meshRenderers[floor] = mrFloor;

    // 2. Create Floating PBR Cube
    auto cube = engine.world.CreateEntity("HeroCube");
    engine.world.transforms[cube].position = Surreal::Vec3(0, 1.0f, 0);
    Surreal::MeshRendererComponent mrCube;
    mrCube.mesh = Surreal::SurrealTerrainBuilder::CreateBox(Surreal::Vec3(1.2f, 1.2f, 1.2f), Surreal::ColorRGB(0.0f, 0.95f, 0.85f));
    mrCube.material.albedo = Surreal::ColorRGB(0.0f, 0.95f, 0.85f);
    mrCube.material.metallic = 0.85f;
    mrCube.material.roughness = 0.25f;
    engine.world.meshRenderers[cube] = mrCube;

    // 3. Setup Camera
    engine.cameraPos = Surreal::Vec3(0, 3.0f, 5.0f);
    engine.cameraTarget = Surreal::Vec3(0, 1.0f, 0);
    engine.cameraUp = Surreal::Vec3(0, 1.0f, 0);

    bool running = true;
    float angle = 0.0f;
    uint32_t lastTicks = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        uint32_t currentTicks = SDL_GetTicks();
        float dt = (currentTicks - lastTicks) / 1000.0f;
        lastTicks = currentTicks;

        // Rotate Cube
        angle += dt * 1.5f;
        engine.world.transforms[cube].rotation = Surreal::Quat::FromAxisAngle(Surreal::Vec3(0, 1.0f, 0), angle);

        // Render Frame
        engine.RenderFrame(dt);

        // Draw HUD overlay
        Surreal::SurrealHUD::DrawPanel(fb.data(), W, H, 15, 15, 340, 65, 0x070B18, 0.85f);
        Surreal::SurrealHUD::DrawString(fb.data(), W, H, 25, 25, "SURREAL ENGINE // STANDALONE APP", 0xFF00FFCC, 1);
        Surreal::SurrealHUD::DrawString(fb.data(), W, H, 25, 45, "Bare-Metal Hybrid 3D Engine for Linux", 0xFF00FF88, 1);

        // Present Frame
        SDL_UpdateTexture(texture, nullptr, fb.data(), W * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
