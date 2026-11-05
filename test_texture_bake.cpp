#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <memory>

struct RawTexture {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = nullptr;

    ~RawTexture() {
        if (data) stbi_image_free(data);
    }
};

int main() {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, "/root/coding/MeinOS/avatar/scene.gltf", &data) != cgltf_result_success) {
        std::cerr << "Failed to parse\n";
        return 1;
    }
    if (cgltf_load_buffers(&options, data, "/root/coding/MeinOS/avatar/scene.bin") != cgltf_result_success) {
        std::cerr << "Failed to load buffers\n";
        return 1;
    }

    std::unordered_map<std::string, std::shared_ptr<RawTexture>> texCache;
    std::string baseDir = "/root/coding/MeinOS/avatar";

    int sampledVerts = 0;
    float sumR = 0, sumG = 0, sumB = 0;

    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const auto& mesh = data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const auto& prim = mesh.primitives[p];
            const cgltf_accessor* posAcc = nullptr;
            const cgltf_accessor* uvAcc = nullptr;

            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                if (prim.attributes[a].type == cgltf_attribute_type_position) posAcc = prim.attributes[a].data;
                if (prim.attributes[a].type == cgltf_attribute_type_texcoord && prim.attributes[a].index == 0) uvAcc = prim.attributes[a].data;
            }

            if (!posAcc) continue;

            std::shared_ptr<RawTexture> tex = nullptr;
            float factor[4] = {1, 1, 1, 1};

            if (prim.material && prim.material->has_pbr_metallic_roughness) {
                const auto& pbr = prim.material->pbr_metallic_roughness;
                factor[0] = pbr.base_color_factor[0];
                factor[1] = pbr.base_color_factor[1];
                factor[2] = pbr.base_color_factor[2];
                factor[3] = pbr.base_color_factor[3];

                if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                    const char* uri = pbr.base_color_texture.texture->image->uri;
                    if (uri) {
                        std::string fullPath = baseDir + "/" + uri;
                        if (texCache.find(fullPath) == texCache.end()) {
                            auto t = std::make_shared<RawTexture>();
                            t->data = stbi_load(fullPath.c_str(), &t->width, &t->height, &t->channels, 4);
                            texCache[fullPath] = t;
                            std::cout << "[+] Loaded texture: " << uri << " (" << t->width << "x" << t->height << ")\n";
                        }
                        tex = texCache[fullPath];
                    }
                }
            }

            for (cgltf_size v = 0; v < posAcc->count; ++v) {
                float r = factor[0], g = factor[1], b = factor[2];
                if (tex && tex->data && uvAcc) {
                    float uv[2] = {0, 0};
                    cgltf_accessor_read_float(uvAcc, v, uv, 2);
                    float u_wrap = uv[0] - std::floor(uv[0]);
                    float v_wrap = uv[1] - std::floor(uv[1]);
                    int tx = std::max(0, std::min(tex->width - 1, static_cast<int>(u_wrap * tex->width)));
                    int ty = std::max(0, std::min(tex->height - 1, static_cast<int>(v_wrap * tex->height)));
                    int pIdx = (ty * tex->width + tx) * 4;

                    r = tex->data[pIdx + 0] / 255.0f;
                    g = tex->data[pIdx + 1] / 255.0f;
                    b = tex->data[pIdx + 2] / 255.0f;

                    // If material has tint (e.g. gloves)
                    if (factor[0] < 0.1f && factor[1] < 0.1f && factor[2] < 0.1f) {
                        r *= factor[0] * 4.0f;
                        g *= factor[1] * 4.0f;
                        b *= factor[2] * 4.0f;
                    }
                } else if (!tex) {
                    // Materials without textures (Underwear, hair cap)
                    r *= 4.0f; g *= 4.0f; b *= 4.0f;
                }

                sumR += r; sumG += g; sumB += b;
                sampledVerts++;
            }
        }
    }

    std::cout << "[✓] Sampled " << sampledVerts << " vertices. Avg RGB: ("
              << (sumR / sampledVerts) << ", " << (sumG / sampledVerts) << ", " << (sumB / sampledVerts) << ")\n";

    cgltf_free(data);
    return 0;
}
