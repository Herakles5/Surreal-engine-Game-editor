#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include <iostream>

int main() {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, "/root/coding/MeinOS/avatar/scene.gltf", &data) != cgltf_result_success) {
        std::cerr << "Failed to parse\n";
        return 1;
    }
    std::cout << "Materials: " << data->materials_count << "\n";
    for (size_t i = 0; i < data->materials_count; ++i) {
        const auto& mat = data->materials[i];
        std::cout << "Mat " << i << ": " << (mat.name ? mat.name : "unnamed");
        if (mat.has_pbr_metallic_roughness) {
            const auto& pbr = mat.pbr_metallic_roughness;
            if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                const auto& img = *pbr.base_color_texture.texture->image;
                std::cout << " -> Image URI: " << (img.uri ? img.uri : "no uri");
            } else {
                std::cout << " -> Factor: (" << pbr.base_color_factor[0] << ", "
                          << pbr.base_color_factor[1] << ", " << pbr.base_color_factor[2] << ")";
            }
        }
        std::cout << "\n";
    }
    cgltf_free(data);
    return 0;
}
