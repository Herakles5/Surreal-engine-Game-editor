/**
 * ============================================================================
 * AION SURREAL 3D ENGINE - Single-TU Third-Party C Implementations
 * Define -DSURREAL_EXTERNAL_THIRD_PARTY_IMPL when linking multiple .cpp files
 * against this compilation unit to prevent ODR / duplicate-symbol errors.
 * ============================================================================
 */

#ifndef CGLTF_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION
#endif
#include "cgltf.h"
#undef CGLTF_IMPLEMENTATION

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"
#undef STB_IMAGE_IMPLEMENTATION
