#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - HIGH-PERFORMANCE glTF 2.0 / GLB 3D MESH LOADER
 * Direct Hybrid Integration for Linux & Bare-Metal CosmOS
 * Converts glTF Primitives into Cache-Dense Surreal Meshlets & PBR Materials
 * ============================================================================
 */

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "surreal_math.hpp"
#include "surreal_pbr.hpp"
#include "surreal_meshlet.hpp"
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <cstring>

namespace Surreal {

struct GLTFModelNode {
    std::string name;
    std::shared_ptr<SurrealMesh> mesh;
    PBRMaterial material;
    Mat4 localTransform;
};

class SurrealGLTFLoader {
public:
    /**
     * Load glTF / GLB model into a list of Mesh Nodes
     */
    static bool LoadModel(
        const std::string& filepath,
        std::vector<GLTFModelNode>& outNodes)
    {
        cgltf_options options;
        std::memset(&options, 0, sizeof(cgltf_options));

        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);

        if (result != cgltf_result_success) {
            std::cerr << "[SurrealGLTF] Failed to parse glTF: " << filepath << " (Code " << result << ")\n";
            return false;
        }

        result = cgltf_load_buffers(&options, data, filepath.c_str());
        if (result != cgltf_result_success) {
            std::cerr << "[SurrealGLTF] Failed to load binary buffers for: " << filepath << "\n";
            cgltf_free(data);
            return false;
        }

        // Parse Materials
        std::vector<PBRMaterial> materials;
        materials.resize(data->materials_count);
        for (cgltf_size i = 0; i < data->materials_count; ++i) {
            const cgltf_material& gMat = data->materials[i];
            PBRMaterial mat;

            if (gMat.has_pbr_metallic_roughness) {
                const auto& mr = gMat.pbr_metallic_roughness;
                mat.albedo = ColorRGB(
                    mr.base_color_factor[0],
                    mr.base_color_factor[1],
                    mr.base_color_factor[2],
                    mr.base_color_factor[3]
                );
                mat.metallic = mr.metallic_factor;
                mat.roughness = mr.roughness_factor;
            }

            mat.emissive = ColorRGB(
                gMat.emissive_factor[0],
                gMat.emissive_factor[1],
                gMat.emissive_factor[2]
            );

            materials[i] = mat;
        }

        // Parse Meshes & Primitives
        for (cgltf_size mIdx = 0; mIdx < data->meshes_count; ++mIdx) {
            const cgltf_mesh& gMesh = data->meshes[mIdx];

            for (cgltf_size pIdx = 0; pIdx < gMesh.primitives_count; ++pIdx) {
                const cgltf_primitive& prim = gMesh.primitives[pIdx];
                if (prim.type != cgltf_primitive_type_triangles) continue;

                auto mesh = std::make_shared<SurrealMesh>();
                PBRMaterial primMat;

                if (prim.material) {
                    cgltf_size matIndex = prim.material - data->materials;
                    if (matIndex < materials.size()) {
                        primMat = materials[matIndex];
                    }
                }

                // Attributes
                const cgltf_accessor* posAcc = nullptr;
                const cgltf_accessor* normAcc = nullptr;
                const cgltf_accessor* uvAcc = nullptr;
                const cgltf_accessor* colAcc = nullptr;

                for (cgltf_size aIdx = 0; aIdx < prim.attributes_count; ++aIdx) {
                    const auto& attr = prim.attributes[aIdx];
                    if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_normal) normAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
                    else if (attr.type == cgltf_attribute_type_color && attr.index == 0) colAcc = attr.data;
                }

                if (!posAcc) continue;

                // Read Vertices
                mesh->vertices.resize(posAcc->count);
                for (cgltf_size v = 0; v < posAcc->count; ++v) {
                    float pos[3] = {0, 0, 0};
                    cgltf_accessor_read_float(posAcc, v, pos, 3);
                    mesh->vertices[v].position = Vec3(pos[0], pos[1], pos[2]);

                    if (normAcc) {
                        float norm[3] = {0, 1.0f, 0};
                        cgltf_accessor_read_float(normAcc, v, norm, 3);
                        mesh->vertices[v].normal = Vec3(norm[0], norm[1], norm[2]).Normalized();
                    } else {
                        mesh->vertices[v].normal = Vec3(0, 1.0f, 0);
                    }

                    if (uvAcc) {
                        float uv[2] = {0, 0};
                        cgltf_accessor_read_float(uvAcc, v, uv, 2);
                        mesh->vertices[v].u = uv[0];
                        mesh->vertices[v].v = uv[1];
                    }

                    if (colAcc) {
                        float c[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                        cgltf_accessor_read_float(colAcc, v, c, 4);
                        mesh->vertices[v].color = Vec4(c[0], c[1], c[2], c[3]);
                    }
                }

                // Read Indices
                if (prim.indices) {
                    mesh->indices.resize(prim.indices->count);
                    for (cgltf_size idx = 0; idx < prim.indices->count; ++idx) {
                        mesh->indices[idx] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, idx));
                    }
                } else {
                    // Non-indexed: generate sequential indices
                    mesh->indices.resize(posAcc->count);
                    for (uint32_t idx = 0; idx < posAcc->count; ++idx) {
                        mesh->indices[idx] = idx;
                    }
                }

                // Partition into Surreal Nanite-style Meshlets
                mesh->BuildClusters();

                GLTFModelNode node;
                node.name = gMesh.name ? gMesh.name : ("Mesh_" + std::to_string(mIdx));
                node.mesh = mesh;
                node.material = primMat;
                node.localTransform = Mat4::Identity();

                outNodes.push_back(node);
            }
        }

        cgltf_free(data);
        return !outNodes.empty();
    }

    /**
     * Helper to load a glTF file and merge all primitives into a single SurrealMesh
     */
    static std::shared_ptr<SurrealMesh> LoadCombinedMesh(
        const std::string& filepath,
        PBRMaterial* outFirstMaterial = nullptr)
    {
        std::vector<GLTFModelNode> nodes;
        if (!LoadModel(filepath, nodes) || nodes.empty()) {
            return nullptr;
        }

        if (outFirstMaterial) {
            *outFirstMaterial = nodes[0].material;
        }

        if (nodes.size() == 1) {
            return nodes[0].mesh;
        }

        // Merge multiple primitives into one consolidated mesh
        auto combined = std::make_shared<SurrealMesh>();
        uint32_t vertexBase = 0;

        for (const auto& node : nodes) {
            for (const auto& v : node.mesh->vertices) {
                combined->vertices.push_back(v);
            }
            for (uint32_t idx : node.mesh->indices) {
                combined->indices.push_back(vertexBase + idx);
            }
            vertexBase += static_cast<uint32_t>(node.mesh->vertices.size());
        }

        combined->BuildClusters();
        return combined;
    }
};

} // namespace Surreal

