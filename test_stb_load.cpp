#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>

int main() {
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load("/root/coding/MeinOS/avatar/textures/Mia_Face_baseColor.png", &w, &h, &c, 4);
    if (!data) {
        std::cerr << "Failed to load image: " << stbi_failure_reason() << "\n";
        return 1;
    }
    std::cout << "Successfully loaded Mia_Face: " << w << "x" << h << ", channels=" << c << "\n";
    // Check center pixel
    int idx = (h / 2 * w + w / 2) * 4;
    std::cout << "Center pixel RGBA: (" << (int)data[idx] << ", " << (int)data[idx+1] << ", "
              << (int)data[idx+2] << ", " << (int)data[idx+3] << ")\n";
    stbi_image_free(data);
    return 0;
}
