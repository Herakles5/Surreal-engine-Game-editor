#pragma once
/**
 * ============================================================================
 * AION SURREAL ENGINE - HIGH-PERFORMANCE glTF 2.0 / GLB 3D MESH LOADER
 * Direct Hybrid Integration for Linux & Bare-Metal CosmOS
 * Converts glTF Primitives into Cache-Dense Surreal Meshlets & PBR Materials
 * ============================================================================
 */

#ifndef SURREAL_EXTERNAL_THIRD_PARTY_IMPL
#ifndef CGLTF_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION
#endif
#endif
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
            mat.doubleSided = (gMat.double_sided != 0);
            mat.alphaCutoff = gMat.alpha_cutoff;
            if (gMat.alpha_mode == cgltf_alpha_mode_mask) mat.alphaMode = PBRMaterial::AlphaMode::MASK_MODE;
            else if (gMat.alpha_mode == cgltf_alpha_mode_blend) mat.alphaMode = PBRMaterial::AlphaMode::BLEND_MODE;
            else mat.alphaMode = PBRMaterial::AlphaMode::OPAQUE_MODE;

            materials[i] = mat;
        }

        auto appendMeshPrimitives = [&](const cgltf_mesh& gMesh, const Mat4& nodeWorld, const std::string& baseName) {
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

                if (prim.indices) {
                    mesh->indices.resize(prim.indices->count);
                    for (cgltf_size idx = 0; idx < prim.indices->count; ++idx) {
                        mesh->indices[idx] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, idx));
                    }
                } else {
                    mesh->indices.resize(posAcc->count);
                    for (uint32_t idx = 0; idx < posAcc->count; ++idx) {
                        mesh->indices[idx] = idx;
                    }
                }

                SubMesh sub;
                sub.indexOffset = 0;
                sub.indexCount = static_cast<uint32_t>(mesh->indices.size());
                sub.albedo = primMat.albedo;
                sub.emissive = primMat.emissive;
                sub.metallic = primMat.metallic;
                sub.roughness = primMat.roughness;
                sub.alphaMode = primMat.alphaMode;
                sub.alphaCutoff = primMat.alphaCutoff;
                sub.doubleSided = primMat.doubleSided;
                mesh->submeshes.push_back(sub);

                mesh->BuildClusters();

                GLTFModelNode node;
                node.name = baseName;
                node.mesh = mesh;
                node.material = primMat;
                node.localTransform = nodeWorld;

                outNodes.push_back(node);
            }
        };

        // Traverse scene graph nodes first so hierarchical world transforms are preserved
        bool loadedAnyNodeMesh = false;
        for (cgltf_size nIdx = 0; nIdx < data->nodes_count; ++nIdx) {
            const cgltf_node* gNode = &data->nodes[nIdx];
            if (!gNode->mesh) continue;
            loadedAnyNodeMesh = true;
            float lm[16];
            cgltf_node_transform_world(gNode, lm);
            Mat4 nodeWorld = Mat4::Identity();
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c) {
                    nodeWorld.m[r][c] = lm[c * 4 + r];
                }
            }
            std::string nName = gNode->name ? gNode->name : (gNode->mesh->name ? gNode->mesh->name : ("Node_" + std::to_string(nIdx)));
            appendMeshPrimitives(*gNode->mesh, nodeWorld, nName);
        }

        // Fallback if glTF contains unattached meshes without scene nodes
        if (!loadedAnyNodeMesh) {
            for (cgltf_size mIdx = 0; mIdx < data->meshes_count; ++mIdx) {
                const cgltf_mesh& gMesh = data->meshes[mIdx];
                std::string mName = gMesh.name ? gMesh.name : ("Mesh_" + std::to_string(mIdx));
                appendMeshPrimitives(gMesh, Mat4::Identity(), mName);
            }
        }

        cgltf_free(data);
        return !outNodes.empty();
    }

    /**
     * Helper to load a glTF file and merge all primitives into a single SurrealMesh (baking node transforms)
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

        auto combined = std::make_shared<SurrealMesh>();
        uint32_t vertexBase = 0;

        for (const auto& node : nodes) {
            Mat4 normMat = node.localTransform.InverseTranspose3x3();
            for (const auto& v : node.mesh->vertices) {
                Vertex3D tv = v;
                tv.position = node.localTransform.TransformPoint(v.position);
                tv.normal = normMat.TransformDirection(v.normal).Normalized();
                combined->vertices.push_back(tv);
            }
            SubMesh sub;
            sub.indexOffset = static_cast<uint32_t>(combined->indices.size());
            sub.indexCount = static_cast<uint32_t>(node.mesh->indices.size());
            sub.albedo = node.material.albedo;
            sub.emissive = node.material.emissive;
            sub.metallic = node.material.metallic;
            sub.roughness = node.material.roughness;
            sub.alphaMode = node.material.alphaMode;
            sub.alphaCutoff = node.material.alphaCutoff;
            sub.doubleSided = node.material.doubleSided;
            for (uint32_t idx : node.mesh->indices) {
                combined->indices.push_back(vertexBase + idx);
            }
            combined->submeshes.push_back(sub);
            vertexBase += static_cast<uint32_t>(node.mesh->vertices.size());
        }

        combined->BuildClusters();
        return combined;
    }
};

} // namespace Surreal

