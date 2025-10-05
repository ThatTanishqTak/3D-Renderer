#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <functional>
#include <unordered_map>
#include <utility>
#include <glm/glm.hpp>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            const glm::vec3 s_DefaultNormal{ 0.0f, 1.0f, 0.0f };
            const glm::vec3 s_DefaultTangent{ 1.0f, 0.0f, 0.0f };
            const glm::vec3 s_DefaultBitangent{ 0.0f, 0.0f, 1.0f };
            const glm::vec3 s_DefaultColor{ 1.0f, 1.0f, 1.0f };
            const glm::vec2 s_DefaultTexCoord{ 0.0f, 0.0f };
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelData l_ModelData{};
            std::string l_NormalizedPath = Utilities::FileManagement::NormalizePath(filePath);
            if (l_NormalizedPath.empty())
            {
                TR_CORE_CRITICAL("Provided model path is empty");
                return l_ModelData;
            }

            Assimp::Importer l_Importer{};
            const unsigned int l_PostProcessFlags =
                aiProcess_Triangulate |          // Ensure all faces are triangles so the renderer can index predictably
                aiProcess_GenSmoothNormals |     // Generate smooth normals when the source omits them
                aiProcess_CalcTangentSpace |     // Derive tangents and bitangents for normal mapping support
                aiProcess_JoinIdenticalVertices |
                aiProcess_ImproveCacheLocality |
                aiProcess_RemoveRedundantMaterials |
                aiProcess_GenUVCoords |
                aiProcess_TransformUVCoords |
                aiProcess_SortByPType;

            const aiScene* l_Scene = l_Importer.ReadFile(l_NormalizedPath, l_PostProcessFlags);
            if (!l_Scene)
            {
                TR_CORE_CRITICAL("Assimp failed to load model '{}': {}", filePath, l_Importer.GetErrorString());
                return l_ModelData;
            }

            if (!l_Scene->mRootNode)
            {
                TR_CORE_CRITICAL("Model '{}' does not contain a root node", filePath);
                return l_ModelData;
            }

            std::string l_BaseDirectory = Utilities::FileManagement::GetBaseDirectory(l_NormalizedPath);
            std::unordered_map<std::string, int> l_TextureLookup{};

            // Helper that registers a texture path once and returns a stable index for all materials.
            const auto RegisterTexture =
                [&](const aiMaterial* a_SourceMaterial, aiTextureType a_Type) -> int
                {
                    if (!a_SourceMaterial || a_SourceMaterial->GetTextureCount(a_Type) == 0)
                    {
                        return -1;
                    }

                    aiString l_TexturePath{};
                    if (a_SourceMaterial->GetTexture(a_Type, 0, &l_TexturePath) != aiReturn_SUCCESS)
                    {
                        TR_CORE_WARN("Failed to read texture slot {} from material", static_cast<int>(a_Type));
                        return -1;
                    }

                    std::string l_PathString = l_TexturePath.C_Str();
                    if (l_PathString.empty())
                    {
                        return -1;
                    }

                    if (!l_PathString.empty() && l_PathString[0] == '*')
                    {
                        TR_CORE_WARN("Embedded textures are not yet supported ({}).", l_PathString.c_str());
                        // TODO: Handle embedded textures by extracting them into GPU-ready images.
                        return -1;
                    }

                    std::string l_ResolvedPath = l_BaseDirectory.empty()
                        ? Utilities::FileManagement::NormalizePath(l_PathString)
                        : Utilities::FileManagement::JoinPath(l_BaseDirectory, l_PathString);

                    auto a_Existing = l_TextureLookup.find(l_ResolvedPath);
                    if (a_Existing != l_TextureLookup.end())
                    {
                        return a_Existing->second;
                    }

                    const int l_NewIndex = static_cast<int>(l_ModelData.Textures.size());
                    l_ModelData.Textures.push_back(l_ResolvedPath);
                    l_TextureLookup.emplace(std::move(l_ResolvedPath), l_NewIndex);

                    return l_NewIndex;
                };

            // Convert every material into the renderer-friendly representation before visiting the scene graph.
            l_ModelData.Materials.reserve(l_Scene->mNumMaterials);
            for (unsigned int it_Material = 0; it_Material < l_Scene->mNumMaterials; ++it_Material)
            {
                const aiMaterial* l_AssimpMaterial = l_Scene->mMaterials[it_Material];
                Geometry::Material l_Material{};

                // Base color factor is preferred from PBR data, falling back to diffuse when unavailable.
                aiColor4D l_BaseColor{};
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_BASE_COLOR, l_BaseColor) == aiReturn_SUCCESS)
                {
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);
                }
                else if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, l_BaseColor) == aiReturn_SUCCESS)
                {
                    l_Material.BaseColorFactor = glm::vec4(l_BaseColor.r, l_BaseColor.g, l_BaseColor.b, l_BaseColor.a);
                }
                else
                {
                    l_Material.BaseColorFactor = glm::vec4(1.0f);
                }

                float l_Metallic = 1.0f;
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_METALLIC_FACTOR, l_Metallic) != aiReturn_SUCCESS)
                {
                    // Legacy formats often store metallic information elsewhere, so we retain the default.
                    l_Metallic = 1.0f;
                }
                l_Material.MetallicFactor = l_Metallic;

                float l_Roughness = 1.0f;
                if (l_AssimpMaterial && l_AssimpMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, l_Roughness) != aiReturn_SUCCESS)
                {
                    // Shininess maps can approximate roughness, but that conversion requires extra work.
                    // TODO: Convert AI_MATKEY_SHININESS into roughness for legacy Phong materials.
                    l_Roughness = 1.0f;
                }
                l_Material.RoughnessFactor = l_Roughness;

                // Texture slots come from modern PBR slots first, then fall back to classic equivalents.
                l_Material.BaseColorTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_BASE_COLOR);
                if (l_Material.BaseColorTextureIndex < 0)
                {
                    l_Material.BaseColorTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_DIFFUSE);
                }

                l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_METALNESS);
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_DIFFUSE_ROUGHNESS);
                }
                if (l_Material.MetallicRoughnessTextureIndex < 0)
                {
                    l_Material.MetallicRoughnessTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_SPECULAR);
                }

                l_Material.NormalTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_NORMALS);
                if (l_Material.NormalTextureIndex < 0)
                {
                    l_Material.NormalTextureIndex = RegisterTexture(l_AssimpMaterial, aiTextureType_HEIGHT);
                }

                // TODO: Surface additional PBR texture slots (emissive, occlusion, clearcoat) for richer shading.

                l_ModelData.Materials.push_back(l_Material);
            }

            // Lambda responsible for turning an aiMesh into Geometry::Mesh instances used by the renderer.
            const auto ConvertMesh =
                [&](const aiMesh* a_AssimpMesh) -> Geometry::Mesh
                {
                    Geometry::Mesh l_Mesh{};
                    if (!a_AssimpMesh)
                    {
                        return l_Mesh;
                    }

                    const unsigned int l_VertexCount = a_AssimpMesh->mNumVertices;
                    l_Mesh.Vertices.resize(l_VertexCount);

                    for (unsigned int it_Vertex = 0; it_Vertex < l_VertexCount; ++it_Vertex)
                    {
                        Vertex l_Vertex{};

                        // Positions are mandatory for rendering; missing data defaults to the origin.
                        if (a_AssimpMesh->HasPositions())
                        {
                            const aiVector3D& l_Position = a_AssimpMesh->mVertices[it_Vertex];
                            l_Vertex.Position = glm::vec3(l_Position.x, l_Position.y, l_Position.z);
                        }
                        else
                        {
                            TR_CORE_WARN("Mesh is missing positions - defaulting to origin");
                            l_Vertex.Position = glm::vec3(0.0f);
                        }

                        // Normals are required for lighting; Assimp can generate them when absent.
                        if (a_AssimpMesh->HasNormals())
                        {
                            const aiVector3D& l_Normal = a_AssimpMesh->mNormals[it_Vertex];
                            l_Vertex.Normal = glm::vec3(l_Normal.x, l_Normal.y, l_Normal.z);
                        }
                        else
                        {
                            l_Vertex.Normal = s_DefaultNormal;
                        }

                        // Tangents/bitangents enable normal mapping, with safe defaults otherwise.
                        if (a_AssimpMesh->HasTangentsAndBitangents())
                        {
                            const aiVector3D& l_Tangent = a_AssimpMesh->mTangents[it_Vertex];
                            const aiVector3D& l_Bitangent = a_AssimpMesh->mBitangents[it_Vertex];
                            l_Vertex.Tangent = glm::vec3(l_Tangent.x, l_Tangent.y, l_Tangent.z);
                            l_Vertex.Bitangent = glm::vec3(l_Bitangent.x, l_Bitangent.y, l_Bitangent.z);
                        }
                        else
                        {
                            l_Vertex.Tangent = s_DefaultTangent;
                            l_Vertex.Bitangent = s_DefaultBitangent;
                        }

                        // Vertex colors provide baked lighting or tint information.
                        if (a_AssimpMesh->HasVertexColors(0))
                        {
                            const aiColor4D& l_Color = a_AssimpMesh->mColors[0][it_Vertex];
                            l_Vertex.Color = glm::vec3(l_Color.r, l_Color.g, l_Color.b);
                        }
                        else
                        {
                            l_Vertex.Color = s_DefaultColor;
                        }

                        // Primary UV set feeds standard material sampling.
                        if (a_AssimpMesh->HasTextureCoords(0))
                        {
                            const aiVector3D& l_Uv = a_AssimpMesh->mTextureCoords[0][it_Vertex];
                            l_Vertex.TexCoord = glm::vec2(l_Uv.x, l_Uv.y);
                        }
                        else
                        {
                            l_Vertex.TexCoord = s_DefaultTexCoord;
                        }

                        // TODO: Support multiple UV sets for advanced shading workflows.

                        l_Mesh.Vertices[it_Vertex] = l_Vertex;
                    }

                    l_Mesh.Indices.reserve(a_AssimpMesh->mNumFaces * 3u);
                    for (unsigned int it_Face = 0; it_Face < a_AssimpMesh->mNumFaces; ++it_Face)
                    {
                        const aiFace& l_Face = a_AssimpMesh->mFaces[it_Face];
                        if (l_Face.mNumIndices != 3)
                        {
                            TR_CORE_WARN("Encountered non-triangle face with {} indices - skipping", l_Face.mNumIndices);
                            continue;
                        }

                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[0]));
                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[1]));
                        l_Mesh.Indices.push_back(static_cast<uint32_t>(l_Face.mIndices[2]));
                    }

                    if (a_AssimpMesh->mMaterialIndex < l_ModelData.Materials.size())
                    {
                        l_Mesh.MaterialIndex = static_cast<int>(a_AssimpMesh->mMaterialIndex);
                    }
                    else
                    {
                        TR_CORE_WARN("Mesh references out-of-range material {}", a_AssimpMesh->mMaterialIndex);
                        l_Mesh.MaterialIndex = -1;
                    }

                    return l_Mesh;
                };

            const std::function<void(const aiNode*)> ProcessNode =
                [&](const aiNode* a_Node)
                {
                    if (!a_Node)
                    {
                        return;
                    }

                    // Convert meshes referenced by the current node.
                    for (unsigned int it_Mesh = 0; it_Mesh < a_Node->mNumMeshes; ++it_Mesh)
                    {
                        const unsigned int l_MeshIndex = a_Node->mMeshes[it_Mesh];
                        if (l_MeshIndex >= l_Scene->mNumMeshes)
                        {
                            TR_CORE_WARN("Node references invalid mesh index {}", l_MeshIndex);
                            continue;
                        }

                        const aiMesh* l_AssimpMesh = l_Scene->mMeshes[l_MeshIndex];
                        Geometry::Mesh l_GeometryMesh = ConvertMesh(l_AssimpMesh);
                        if (!l_GeometryMesh.Vertices.empty())
                        {
                            l_ModelData.Meshes.push_back(std::move(l_GeometryMesh));
                        }
                    }

                    // Recurse through children to cover the full scene graph.
                    for (unsigned int it_Child = 0; it_Child < a_Node->mNumChildren; ++it_Child)
                    {
                        ProcessNode(a_Node->mChildren[it_Child]);
                    }
                };

            ProcessNode(l_Scene->mRootNode);

            return l_ModelData;
        }
    }
}