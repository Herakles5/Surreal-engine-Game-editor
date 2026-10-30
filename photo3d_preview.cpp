/**
 * AION SURREAL - Photo-to-3D preview tool (CPU rasterizer, no GPU / window needed)
 *
 *   ./surreal-photo3d <image.png|jpg> [out_prefix] [maxRes]
 *
 * Builds a coloured 3D mesh from the picture (see surreal_image3d.hpp) and renders
 * out_prefix_front.png / _side.png / _back.png so the result can be checked quickly
 * (also by Aion herself, who can look at the PNGs).
 */
#include "surreal_engine.hpp"
#include "surreal_animation.hpp"   // provides the stb_image implementation
#include "surreal_image3d.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>

static uint32_t Crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t table[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// Minimal PNG writer (stored/uncompressed deflate blocks) - enough for previews.
static bool WritePNG(const std::string& path, const std::vector<uint8_t>& rgb, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * w * 3, rgb.begin() + static_cast<size_t>(y + 1) * w * 3); }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back((n >> 8) & 0xFF);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((ad >> s) & 0xFF);

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [](uint32_t v) { uint8_t o[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; return std::vector<uint8_t>(o, o + 4); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        auto len = be32(static_cast<uint32_t>(data.size()));
        fwrite(len.data(), 1, 4, f);
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        fwrite(td.data(), 1, td.size(), f);
        auto crc = be32(Crc32(td.data(), td.size()));
        fwrite(crc.data(), 1, 4, f);
    };
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    for (uint32_t v : {uint32_t(w), uint32_t(h)}) { auto t = be32(v); ihdr.insert(ihdr.end(), t.begin(), t.end()); }
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <image.png|jpg> [out_prefix] [maxRes]\n";
        return 1;
    }
    std::string imgPath = argv[1];
    std::string prefix = argc > 2 ? argv[2] : "photo3d_preview";
    Surreal::Image3DOptions opt;
    if (argc > 3) opt.maxRes = std::max(40, std::atoi(argv[3]));

    Surreal::Image3DInfo info;
    auto mesh = Surreal::SurrealImage3D::BuildFromFile(imgPath, opt, &info);
    if (!mesh) {
        std::cerr << "[-] " << info.error << "\n";
        return 2;
    }
    std::cout << "[OK] Grid " << info.gridW << "x" << info.gridH << " | Objekt-Zellen: " << info.maskCells
              << " | " << (info.usedAlpha ? "Alpha-Freisteller" : (info.plateMode ? "Platte (kein Hintergrund erkannt)" : "Hintergrund entfernt"))
              << " | Verts: " << mesh->vertices.size() << " Tris: " << mesh->indices.size() / 3 << "\n";
    Surreal::Vec3 bmin = mesh->worldBounds.min, bmax = mesh->worldBounds.max;
    std::cout << "     Masse (m): B=" << (bmax.x - bmin.x) << " H=" << (bmax.y - bmin.y) << " T=" << (bmax.z - bmin.z) << "\n";

    constexpr uint32_t W = 640, H = 800;
    std::vector<uint32_t> frame(W * H, 0);
    Surreal::SurrealEngine engine(W, H);
    engine.AttachFramebuffer(frame.data(), W * sizeof(uint32_t));

    Surreal::EntityID e = engine.world.CreateEntity("Photo3D");
    Surreal::MeshRendererComponent mr;
    mr.mesh = mesh;
    mr.material.albedo = Surreal::ColorRGB(1, 1, 1);
    mr.material.metallic = 0.05f;
    mr.material.roughness = 0.8f;
    engine.world.meshRenderers[e] = mr;
    engine.world.ambientLight = Surreal::ColorRGB(0.35f, 0.35f, 0.38f);

    float height = bmax.y - bmin.y;
    float dist = std::max(height * 1.9f, (bmax.x - bmin.x) * 2.2f);
    Surreal::Vec3 target(0, height * 0.5f, 0);
    const char* names[3] = {"front", "side", "back"};
    float yaws[3] = {0.0f, 0.95f, 3.14159f};

    for (int v = 0; v < 3; ++v) {
        float yaw = yaws[v];
        engine.cameraTarget = target;
        engine.cameraPos = Surreal::Vec3(std::sin(yaw) * dist, height * 0.55f, std::cos(yaw) * dist);
        engine.cameraFov = 35.0f * 3.14159f / 180.0f;
        Surreal::Vec3 toCam = (engine.cameraPos - target).Normalized();
        engine.world.sunLight.direction = (toCam + Surreal::Vec3(0.25f, 0.7f, 0.0f)).Normalized();
        engine.RenderFrame(0.016f);

        std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
        for (uint32_t i = 0; i < W * H; ++i) {
            bool bg = engine.depthBuffer[i] >= 99999.0f;
            uint32_t p = frame[i];
            uint8_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            if (bg) { float t = static_cast<float>(i / W) / H; r = g = b = static_cast<uint8_t>(205 - 40 * t); }
            rgb[i * 3 + 0] = r; rgb[i * 3 + 1] = g; rgb[i * 3 + 2] = b;
        }
        std::string out = prefix + "_" + names[v] + ".png";
        if (WritePNG(out, rgb, W, H)) std::cout << "     -> " << out << "\n";
        else std::cerr << "[-] Konnte " << out << " nicht schreiben\n";
    }
    return 0;
}

