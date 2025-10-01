#include "Loader/ModelLoader.h"
#include "Core/Utilities.h"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <filesystem>
#include <unordered_map>
#include <cstring>
#include <utility>
#include <cstdint>
#include <glm/gtx/norm.hpp>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            const glm::vec3 s_DefaultNormal{ 0.0f, 1.0f, 0.0f };
            const glm::vec3 s_DefaultTangent{ 0.0f, 0.0f, 0.0f };
            const glm::vec3 s_DefaultBitangent{ 0.0f, 0.0f, 0.0f };

            // Prepare a pointer/stride pair that can be used to iterate over accessor data safely
            static bool PrepareAccessorRead(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t componentCount,
                size_t componentSize, const unsigned char*& dataPointer, size_t& stride)
            {
                if (accessor.count == 0 || accessor.bufferView < 0)
                {
                    return false;
                }

                if (accessor.sparse.count > 0)
                {
                    TR_CORE_WARN("Sparse accessor detected ({} elements) - sparse data is not yet supported", accessor.sparse.count);
                }

                const tinygltf::BufferView& l_View = model.bufferViews[accessor.bufferView];
                const tinygltf::Buffer& l_Buffer = model.buffers[l_View.buffer];

                dataPointer = l_Buffer.data.data() + l_View.byteOffset + accessor.byteOffset;
                stride = l_View.byteStride != 0 ? static_cast<size_t>(l_View.byteStride) : componentCount * componentSize;

                return true;
            }

            static void LoadPositions(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec3>& out)
            {
                out.clear();
                if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    TR_CORE_WARN("Unsupported component type {} for POSITION attribute", accessor.componentType);
                    return;
                }

                const unsigned char* a_Data = nullptr;
                size_t l_Stride = 0;
                if (!PrepareAccessorRead(model, accessor, 3, sizeof(float), a_Data, l_Stride))
                {
                    return;
                }

                out.resize(accessor.count);
                for (size_t it_Index = 0; it_Index < out.size(); ++it_Index)
                {
                    const float* l_Source = reinterpret_cast<const float*>(a_Data + it_Index * l_Stride);
                    out[it_Index] = glm::vec3(l_Source[0], l_Source[1], l_Source[2]);
                }
            }

            static void LoadNormals(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec3>& out)
            {
                out.clear();
                if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    TR_CORE_WARN("Unsupported component type {} for NORMAL attribute", accessor.componentType);
                    return;
                }

                const unsigned char* a_Data = nullptr;
                size_t l_Stride = 0;
                if (!PrepareAccessorRead(model, accessor, 3, sizeof(float), a_Data, l_Stride))
                {
                    return;
                }

                out.resize(accessor.count);
                for (size_t it_Index = 0; it_Index < out.size(); ++it_Index)
                {
                    const float* l_Source = reinterpret_cast<const float*>(a_Data + it_Index * l_Stride);
                    out[it_Index] = glm::vec3(l_Source[0], l_Source[1], l_Source[2]);
                }
            }

            static void LoadTangents(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec4>& out)
            {
                out.clear();
                if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    TR_CORE_WARN("Unsupported component type {} for TANGENT attribute", accessor.componentType);
                    return;
                }

                const unsigned char* a_Data = nullptr;
                size_t l_Stride = 0;
                if (!PrepareAccessorRead(model, accessor, 4, sizeof(float), a_Data, l_Stride))
                {
                    return;
                }

                out.resize(accessor.count);
                for (size_t it_Index = 0; it_Index < out.size(); ++it_Index)
                {
                    const float* l_Source = reinterpret_cast<const float*>(a_Data + it_Index * l_Stride);
                    out[it_Index] = glm::vec4(l_Source[0], l_Source[1], l_Source[2], l_Source[3]);
                }
            }

            static void LoadTexCoords(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec2>& out)
            {
                out.clear();
                if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    TR_CORE_WARN("Unsupported component type {} for TEXCOORD_0 attribute", accessor.componentType);
                    return;
                }

                const unsigned char* a_Data = nullptr;
                size_t l_Stride = 0;
                if (!PrepareAccessorRead(model, accessor, 2, sizeof(float), a_Data, l_Stride))
                {
                    return;
                }

                out.resize(accessor.count);
                for (size_t it_Index = 0; it_Index < out.size(); ++it_Index)
                {
                    const float* l_Source = reinterpret_cast<const float*>(a_Data + it_Index * l_Stride);
                    out[it_Index] = glm::vec2(l_Source[0], l_Source[1]);
                }
            }

            static void LoadColors(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec3>& out)
            {
                out.clear();
                const size_t l_ComponentCount = accessor.type == TINYGLTF_TYPE_VEC4 ? 4 : 3;
                const size_t l_ComponentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
                if (l_ComponentSize == 0)
                {
                    TR_CORE_WARN("Unsupported component type {} for COLOR_0 attribute", accessor.componentType);
                    return;
                }

                const unsigned char* a_Data = nullptr;
                size_t l_Stride = 0;
                if (!PrepareAccessorRead(model, accessor, l_ComponentCount, l_ComponentSize, a_Data, l_Stride))
                {
                    return;
                }

                out.resize(accessor.count, glm::vec3(1.0f));
                for (size_t it_Index = 0; it_Index < out.size(); ++it_Index)
                {
                    glm::vec3 l_Color{ 1.0f };
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                    {
                        const float* l_Source = reinterpret_cast<const float*>(a_Data + it_Index * l_Stride);
                        l_Color.r = l_Source[0];
                        l_Color.g = l_Source[1];
                        l_Color.b = l_ComponentCount > 2 ? l_Source[2] : 1.0f;
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        const uint8_t* l_Source = reinterpret_cast<const uint8_t*>(a_Data + it_Index * l_Stride);
                        l_Color.r = static_cast<float>(l_Source[0]) / 255.0f;
                        l_Color.g = static_cast<float>(l_Source[1]) / 255.0f;
                        l_Color.b = l_ComponentCount > 2 ? static_cast<float>(l_Source[2]) / 255.0f : 1.0f;
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        const uint16_t* l_Source = reinterpret_cast<const uint16_t*>(a_Data + it_Index * l_Stride);
                        l_Color.r = static_cast<float>(l_Source[0]) / 65535.0f;
                        l_Color.g = static_cast<float>(l_Source[1]) / 65535.0f;
                        l_Color.b = l_ComponentCount > 2 ? static_cast<float>(l_Source[2]) / 65535.0f : 1.0f;
                    }
                    else
                    {
                        TR_CORE_WARN("Unsupported component type {} for COLOR_0 attribute", accessor.componentType);
                        return;
                    }

                    out[it_Index] = l_Color;
                }
            }

            static void LoadIndices(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<uint32_t>& out)
            {
                out.clear();
                const size_t l_ComponentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
                if (l_ComponentSize == 0)
                {
                    TR_CORE_WARN("Unsupported component type {} for index buffer", accessor.componentType);
                    return;
                }

                const unsigned char* a_Data = nullptr;
                size_t l_Stride = 0;
                if (!PrepareAccessorRead(model, accessor, 1, l_ComponentSize, a_Data, l_Stride))
                {
                    return;
                }

                out.resize(accessor.count);
                for (size_t it_Index = 0; it_Index < out.size(); ++it_Index)
                {
                    const unsigned char* l_Element = a_Data + it_Index * l_Stride;
                    switch (accessor.componentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    {
                        uint8_t l_Value = 0;
                        std::memcpy(&l_Value, l_Element, sizeof(uint8_t));
                        out[it_Index] = l_Value;
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    {
                        uint16_t l_Value = 0;
                        std::memcpy(&l_Value, l_Element, sizeof(uint16_t));
                        out[it_Index] = l_Value;
                        break;
                    }
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    {
                        uint32_t l_Value = 0;
                        std::memcpy(&l_Value, l_Element, sizeof(uint32_t));
                        out[it_Index] = l_Value;
                        break;
                    }
                    default:
                        TR_CORE_WARN("Unsupported index component type {}", accessor.componentType);
                        out.clear();
                        return;
                    }
                }
            }
        }

        ModelData ModelLoader::Load(const std::string& filePath)
        {
            ModelData l_ModelData{};
            std::filesystem::path l_Path = Utilities::FileManagement::NormalizePath(filePath);
            if (l_Path.extension() != ".gltf" && l_Path.extension() != ".glb")
            {
                TR_CORE_CRITICAL("Unsupported model format: {}", filePath);
                return l_ModelData;
            }

            tinygltf::TinyGLTF l_Loader;
            tinygltf::Model l_Model;
            std::string l_Error;
            std::string l_Warning;
            bool l_Result = false;
            if (l_Path.extension() == ".glb")
            {
                l_Result = l_Loader.LoadBinaryFromFile(&l_Model, &l_Error, &l_Warning, l_Path.string());
            }
            else
            {
                l_Result = l_Loader.LoadASCIIFromFile(&l_Model, &l_Error, &l_Warning, l_Path.string());
            }

            if (!l_Warning.empty())
            {
                TR_CORE_WARN("{}", l_Warning);
            }
            if (!l_Error.empty())
            {
                TR_CORE_ERROR("{}", l_Error);
            }
            if (!l_Result)
            {
                TR_CORE_CRITICAL("Failed to load model: {}", filePath);
                return l_ModelData;
            }

            std::unordered_map<int, int> l_MaterialRemap{};

            for (const tinygltf::Mesh& l_Mesh : l_Model.meshes)
            {
                for (const tinygltf::Primitive& l_Primitive : l_Mesh.primitives)
                {
                    Geometry::Mesh l_MeshData{};

                    auto a_PositionIt = l_Primitive.attributes.find("POSITION");
                    if (a_PositionIt == l_Primitive.attributes.end())
                    {
                        TR_CORE_ERROR("Primitive in {} is missing POSITION data - skipping", filePath);
                        continue;
                    }

                    const tinygltf::Accessor& l_PositionAccessor = l_Model.accessors[a_PositionIt->second];
                    std::vector<glm::vec3> l_Positions{};
                    LoadPositions(l_Model, l_PositionAccessor, l_Positions);
                    if (l_Positions.empty())
                    {
                        TR_CORE_WARN("Primitive in {} produced no position data - skipping", filePath);
                        continue;
                    }

                    // Optional attributes collected into scratch arrays before building vertices
                    std::vector<glm::vec3> l_Normals{};
                    bool l_HasNormals = false;
                    if (auto a_NormalIt = l_Primitive.attributes.find("NORMAL"); a_NormalIt != l_Primitive.attributes.end())
                    {
                        const tinygltf::Accessor& l_NormalAccessor = l_Model.accessors[a_NormalIt->second];
                        LoadNormals(l_Model, l_NormalAccessor, l_Normals);
                        l_HasNormals = !l_Normals.empty();
                    }
                    if (!l_HasNormals)
                    {
                        TR_CORE_WARN("Primitive in {} is missing normals - applying default normal", filePath);
                        // TODO: Derive missing normals procedurally when topology allows it.
                    }

                    std::vector<glm::vec4> l_Tangents{};
                    if (auto a_TangentIt = l_Primitive.attributes.find("TANGENT"); a_TangentIt != l_Primitive.attributes.end())
                    {
                        const tinygltf::Accessor& l_TangentAccessor = l_Model.accessors[a_TangentIt->second];
                        LoadTangents(l_Model, l_TangentAccessor, l_Tangents);
                    }

                    std::vector<glm::vec2> l_TexCoords{};
                    if (auto a_TexCoordIt = l_Primitive.attributes.find("TEXCOORD_0"); a_TexCoordIt != l_Primitive.attributes.end())
                    {
                        const tinygltf::Accessor& l_TexCoordAccessor = l_Model.accessors[a_TexCoordIt->second];
                        LoadTexCoords(l_Model, l_TexCoordAccessor, l_TexCoords);
                    }

                    std::vector<glm::vec3> l_Colors{};
                    if (auto a_ColorIt = l_Primitive.attributes.find("COLOR_0"); a_ColorIt != l_Primitive.attributes.end())
                    {
                        const tinygltf::Accessor& l_ColorAccessor = l_Model.accessors[a_ColorIt->second];
                        LoadColors(l_Model, l_ColorAccessor, l_Colors);
                    }

                    std::vector<uint32_t> l_Indices{};
                    if (l_Primitive.indices >= 0 && l_Primitive.indices < static_cast<int>(l_Model.accessors.size()))
                    {
                        const tinygltf::Accessor& l_IndexAccessor = l_Model.accessors[l_Primitive.indices];
                        LoadIndices(l_Model, l_IndexAccessor, l_Indices);
                    }
                    else if (l_Primitive.indices >= static_cast<int>(l_Model.accessors.size()))
                    {
                        TR_CORE_WARN("Primitive references invalid index accessor {} - indices skipped", l_Primitive.indices);
                    }

                    l_MeshData.Vertices.resize(l_Positions.size());
                    for (size_t it_Vertex = 0; it_Vertex < l_Positions.size(); ++it_Vertex)
                    {
                        Vertex l_Vertex{};
                        l_Vertex.Position = l_Positions[it_Vertex];
                        l_Vertex.Color = it_Vertex < l_Colors.size() ? l_Colors[it_Vertex] : glm::vec3(1.0f);
                        l_Vertex.TexCoord = it_Vertex < l_TexCoords.size() ? l_TexCoords[it_Vertex] : glm::vec2(0.0f);
                        l_Vertex.Normal = (l_HasNormals && it_Vertex < l_Normals.size()) ? l_Normals[it_Vertex] : s_DefaultNormal;

                        if (!l_HasNormals || glm::length2(l_Vertex.Normal) == 0.0f)
                        {
                            l_Vertex.Normal = s_DefaultNormal;
                        }
                        else
                        {
                            l_Vertex.Normal = glm::normalize(l_Vertex.Normal);
                        }

                        if (it_Vertex < l_Tangents.size())
                        {
                            const glm::vec4& l_Tangent = l_Tangents[it_Vertex];
                            glm::vec3 l_TangentVec = glm::vec3(l_Tangent);
                            if (glm::length2(l_TangentVec) > 0.0f)
                            {
                                l_TangentVec = glm::normalize(l_TangentVec);
                                l_Vertex.Tangent = l_TangentVec;
                                glm::vec3 l_Bitangent = glm::cross(l_Vertex.Normal, l_TangentVec);
                                if (glm::length2(l_Bitangent) > 0.0f)
                                {
                                    l_Vertex.Bitangent = glm::normalize(l_Bitangent) * l_Tangent.w;
                                }
                                else
                                {
                                    l_Vertex.Bitangent = s_DefaultBitangent;
                                }
                            }
                            else
                            {
                                l_Vertex.Tangent = s_DefaultTangent;
                                l_Vertex.Bitangent = s_DefaultBitangent;
                            }
                        }
                        else
                        {
                            l_Vertex.Tangent = s_DefaultTangent;
                            l_Vertex.Bitangent = s_DefaultBitangent;
                            // TODO: Reconstruct tangents from UVs when source data omits them.
                        }

                        l_MeshData.Vertices[it_Vertex] = l_Vertex;
                    }

                    l_MeshData.Indices = std::move(l_Indices);

                    const int l_GltfMaterialIndex = l_Primitive.material;
                    if (l_GltfMaterialIndex >= 0 && l_GltfMaterialIndex < static_cast<int>(l_Model.materials.size()))
                    {
                        auto a_RemappedIt = l_MaterialRemap.find(l_GltfMaterialIndex);
                        if (a_RemappedIt == l_MaterialRemap.end())
                        {
                            const tinygltf::Material& l_SourceMaterial = l_Model.materials[l_GltfMaterialIndex];
                            Geometry::Material l_Material{};
                            const auto& l_Pbr = l_SourceMaterial.pbrMetallicRoughness;

                            if (l_Pbr.baseColorFactor.size() == 4)
                            {
                                l_Material.BaseColorFactor = glm::vec4
                                (
                                    static_cast<float>(l_Pbr.baseColorFactor[0]),
                                    static_cast<float>(l_Pbr.baseColorFactor[1]),
                                    static_cast<float>(l_Pbr.baseColorFactor[2]),
                                    static_cast<float>(l_Pbr.baseColorFactor[3])
                                );
                            }

                            l_Material.MetallicFactor = static_cast<float>(l_Pbr.metallicFactor);
                            l_Material.RoughnessFactor = static_cast<float>(l_Pbr.roughnessFactor);
                            l_Material.BaseColorTextureIndex = l_Pbr.baseColorTexture.index;
                            l_Material.MetallicRoughnessTextureIndex = l_Pbr.metallicRoughnessTexture.index;
                            l_Material.NormalTextureIndex = l_SourceMaterial.normalTexture.index;

                            if (l_Material.MetallicFactor == 1.0f && l_Material.RoughnessFactor == 1.0f && l_Material.MetallicRoughnessTextureIndex < 0)
                            {
                                TR_CORE_WARN("Material '{}' in {} is missing metallic/roughness data - falling back to defaults", l_SourceMaterial.name.c_str(), filePath);
                            }

                            const int l_NewMaterialIndex = static_cast<int>(l_ModelData.Materials.size());
                            l_ModelData.Materials.push_back(l_Material);
                            l_MaterialRemap.emplace(l_GltfMaterialIndex, l_NewMaterialIndex);
                            l_MeshData.MaterialIndex = l_NewMaterialIndex;
                        }
                        else
                        {
                            l_MeshData.MaterialIndex = a_RemappedIt->second;
                        }
                    }
                    else
                    {
                        TR_CORE_WARN("Primitive in {} has no material assigned - defaults will be used", filePath);
                        l_MeshData.MaterialIndex = -1;
                    }

                    l_ModelData.Meshes.push_back(std::move(l_MeshData));
                }
            }

            return l_ModelData;
        }
    }
}