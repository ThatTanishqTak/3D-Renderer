#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            static std::string ExtractSection(const std::string& text, std::string_view token)
            {
                size_t l_Start = text.find(token.data());
                if (l_Start == std::string::npos)
                {
                    return {};
                }

                l_Start = text.find('{', l_Start);
                if (l_Start == std::string::npos)
                {
                    return {};
                }

                size_t l_End = text.find('}', l_Start);
                if (l_End == std::string::npos)
                {
                    return {};
                }

                return text.substr(l_Start + 1, l_End - l_Start - 1);
            }

            static std::vector<float> ParseFloats(std::string_view data)
            {
                std::vector<float> l_Result;
                std::stringstream l_Stream{ std::string(data) };
                std::string l_Token;
                
                while (std::getline(l_Stream, l_Token, ','))
                {
                    if (!l_Token.empty())
                    {
                        l_Result.push_back(std::stof(l_Token));
                    }
                }

                return l_Result;
            }

            static std::vector<int> ParseInts(std::string_view data)
            {
                std::vector<int> l_Result;
                std::stringstream l_Stream{ std::string(data) };
                std::string l_Token;
                
                while (std::getline(l_Stream, l_Token, ','))
                {
                    if (!l_Token.empty())
                    {
                        l_Result.push_back(std::stoi(l_Token));
                    }
                }

                return l_Result;
            }

            static bool LoadFBX(const std::filesystem::path& path, Geometry::Mesh& mesh)
            {
                std::ifstream l_File(path, std::ios::binary);
                if (!l_File.is_open())
                {
                    TR_CORE_ERROR("Failed to open FBX file: {}", path.string());
                    
                    return false;
                }

                std::string l_Content((std::istreambuf_iterator<char>(l_File)), {});
                std::string l_VertData = ExtractSection(l_Content, "Vertices:");
                std::string l_IndexData = ExtractSection(l_Content, "PolygonVertexIndex:");

                if (l_VertData.empty() || l_IndexData.empty())
                {
                    return false;
                }

                auto l_Verts = ParseFloats(l_VertData);
                auto l_Indices = ParseInts(l_IndexData);

                mesh.Vertices.clear();
                mesh.Indices.clear();

                for (size_t i = 0; i + 2 < l_Verts.size(); i += 3)
                {
                    Vertex l_Vertex{};
                    l_Vertex.Position = { l_Verts[i], l_Verts[i + 1], l_Verts[i + 2] };
                    l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                    mesh.Vertices.push_back(l_Vertex);
                }

                std::vector<uint32_t> l_Polygon;
                for (int l_Value : l_Indices)
                {
                    bool l_End = l_Value < 0;
                    uint32_t l_Index = static_cast<uint32_t>(l_End ? ~l_Value : l_Value);
                    l_Polygon.push_back(l_Index);
                    if (l_End)
                    {
                        for (size_t i = 1; i + 1 < l_Polygon.size(); ++i)
                        {
                            mesh.Indices.push_back(l_Polygon[0]);
                            mesh.Indices.push_back(l_Polygon[i]);
                            mesh.Indices.push_back(l_Polygon[i + 1]);
                        }
                        l_Polygon.clear();
                    }
                }

                return !mesh.Vertices.empty();
            }

            static bool LoadOBJ(const std::filesystem::path& path, std::vector<Geometry::Mesh>& meshes)
            {
                std::ifstream l_File(path);
                if (!l_File.is_open())
                {
                    TR_CORE_ERROR("Failed to open OBJ file: {}", path.string());
                    return false;
                }

                std::vector<glm::vec3> l_Positions;
                std::vector<glm::vec2> l_Texcoords;
                Geometry::Mesh l_Mesh{};
                std::unordered_map<std::string, uint32_t> l_Unique;
                std::string l_Line;

                while (std::getline(l_File, l_Line))
                {
                    std::stringstream l_Stream(l_Line);
                    std::string l_Prefix;
                    l_Stream >> l_Prefix;

                    if (l_Prefix == "v")
                    {
                        glm::vec3 l_Pos;
                        l_Stream >> l_Pos.x >> l_Pos.y >> l_Pos.z;
                        l_Positions.push_back(l_Pos);
                    }
                    else if (l_Prefix == "vt")
                    {
                        glm::vec2 l_UV;
                        l_Stream >> l_UV.x >> l_UV.y;
                        l_Texcoords.push_back(l_UV);
                    }
                    else if (l_Prefix == "f")
                    {
                        std::vector<uint32_t> l_Face;
                        std::string l_VertexData;
                        
                        while (l_Stream >> l_VertexData)
                        {
                            auto l_It = l_Unique.find(l_VertexData);
                            if (l_It == l_Unique.end())
                            {
                                uint32_t l_PosIndex = 0;
                                uint32_t l_TexIndex = 0;

                                size_t l_First = l_VertexData.find('/');
                                size_t l_Second = l_VertexData.find('/', l_First + 1);

                                if (l_First == std::string::npos)
                                {
                                    l_PosIndex = std::stoul(l_VertexData);
                                }
                                else
                                {
                                    l_PosIndex = std::stoul(l_VertexData.substr(0, l_First));
                                    if (l_Second != std::string::npos && l_Second > l_First + 1)
                                    {
                                        l_TexIndex = std::stoul(l_VertexData.substr(l_First + 1, l_Second - l_First - 1));
                                    }
                                    else if (l_First + 1 < l_VertexData.size())
                                    {
                                        l_TexIndex = std::stoul(l_VertexData.substr(l_First + 1));
                                    }
                                }

                                Vertex l_Vertex{};
                                if (l_PosIndex > 0 && l_PosIndex <= l_Positions.size())
                                {
                                    l_Vertex.Position = l_Positions[l_PosIndex - 1];
                                }
                        
                                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                                if (l_TexIndex > 0 && l_TexIndex <= l_Texcoords.size())
                                {
                                    glm::vec2 l_UVCoord = l_Texcoords[l_TexIndex - 1];
                                    l_Vertex.TexCoord = { l_UVCoord.x, 1.0f - l_UVCoord.y };
                                }

                                uint32_t l_Index = static_cast<uint32_t>(l_Mesh.Vertices.size());
                                l_Mesh.Vertices.push_back(l_Vertex);
                                l_Unique.emplace(l_VertexData, l_Index);
                                l_Face.push_back(l_Index);
                            }
                            else
                            {
                                l_Face.push_back(l_It->second);
                            }
                        }

                        for (size_t i = 1; i + 1 < l_Face.size(); ++i)
                        {
                            l_Mesh.Indices.push_back(l_Face[0]);
                            l_Mesh.Indices.push_back(l_Face[i]);
                            l_Mesh.Indices.push_back(l_Face[i + 1]);
                        }
                    }
                }

                if (!l_Mesh.Vertices.empty())
                {
                    meshes.push_back(std::move(l_Mesh));
                    
                    return true;
                }

                return false;
            }

            static bool LoadGLTF(const std::filesystem::path& path, std::vector<Geometry::Mesh>& meshes)
            {
                tinygltf::TinyGLTF l_Loader;
                tinygltf::Model l_Model{};
                std::string l_Err;
                std::string l_Warn;

                bool l_Binary = path.extension() == ".glb";
                bool l_Loaded = l_Binary ? l_Loader.LoadBinaryFromFile(&l_Model, &l_Err, &l_Warn, path.string()) : l_Loader.LoadASCIIFromFile(&l_Model, &l_Err, &l_Warn, path.string());

                if (!l_Warn.empty())
                {
                    TR_CORE_WARN("{}", l_Warn);
                }

                if (!l_Loaded)
                {
                    TR_CORE_CRITICAL("Failed to load model: {} ({})", path.string(), l_Err);
                    return false;
                }

                for (const auto& l_SrcMesh : l_Model.meshes)
                {
                    for (const auto& l_Prim : l_SrcMesh.primitives)
                    {
                        auto l_PosIt = l_Prim.attributes.find("POSITION");
                        if (l_PosIt == l_Prim.attributes.end())
                        {
                            TR_CORE_CRITICAL("Primitive has no POSITION attribute");
                            
                            continue;
                        }

                        Geometry::Mesh l_Mesh{};

                        const tinygltf::Accessor& l_PosAcc = l_Model.accessors[l_PosIt->second];
                        const tinygltf::BufferView& l_PosView = l_Model.bufferViews[l_PosAcc.bufferView];
                        const tinygltf::Buffer& l_PosBuf = l_Model.buffers[l_PosView.buffer];
                        const float* l_Positions = reinterpret_cast<const float*>(&l_PosBuf.data[l_PosView.byteOffset + l_PosAcc.byteOffset]);

                        const float* l_Texcoords = nullptr;
                        bool l_HasTex = false;
                        auto l_TexIt = l_Prim.attributes.find("TEXCOORD_0");
                        if (l_TexIt != l_Prim.attributes.end())
                        {
                            const tinygltf::Accessor& l_TexAcc = l_Model.accessors[l_TexIt->second];
                            const tinygltf::BufferView& l_TexView = l_Model.bufferViews[l_TexAcc.bufferView];
                            const tinygltf::Buffer& l_TexBuf = l_Model.buffers[l_TexView.buffer];
                            l_Texcoords = reinterpret_cast<const float*>(&l_TexBuf.data[l_TexView.byteOffset + l_TexAcc.byteOffset]);
                            l_HasTex = true;
                        }

                        l_Mesh.Vertices.reserve(l_PosAcc.count);
                        for (size_t i = 0; i < l_PosAcc.count; ++i)
                        {
                            Vertex l_Vertex{};
                            l_Vertex.Position = { l_Positions[i * 3], l_Positions[i * 3 + 1], l_Positions[i * 3 + 2] };
                            l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                            if (l_HasTex)
                            {
                                l_Vertex.TexCoord = { l_Texcoords[i * 2], 1.0f - l_Texcoords[i * 2 + 1] };
                            }
                            l_Mesh.Vertices.push_back(l_Vertex);
                        }

                        if (l_Prim.indices >= 0)
                        {
                            const tinygltf::Accessor& l_IndAcc = l_Model.accessors[l_Prim.indices];
                            const tinygltf::BufferView& l_IndView = l_Model.bufferViews[l_IndAcc.bufferView];
                            const tinygltf::Buffer& l_IndBuf = l_Model.buffers[l_IndView.buffer];

                            l_Mesh.Indices.reserve(l_IndAcc.count);
                            if (l_IndAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                            {
                                const uint16_t* l_Indices = reinterpret_cast<const uint16_t*>(&l_IndBuf.data[l_IndView.byteOffset + l_IndAcc.byteOffset]);
                                for (size_t i = 0; i < l_IndAcc.count; ++i)
                                {
                                    l_Mesh.Indices.push_back(l_Indices[i]);
                                }
                            }
                            else if (l_IndAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                            {
                                const uint32_t* l_Indices = reinterpret_cast<const uint32_t*>(&l_IndBuf.data[l_IndView.byteOffset + l_IndAcc.byteOffset]);
                                l_Mesh.Indices.insert(l_Mesh.Indices.end(), l_Indices, l_Indices + l_IndAcc.count);
                            }
                        }

                        if (!l_Mesh.Vertices.empty())
                        {
                            meshes.push_back(std::move(l_Mesh));
                        }
                    }
                }

                return !meshes.empty();
            }
        }

        std::vector<Geometry::Mesh> ModelLoader::Load(const std::string& filePath)
        {
            std::vector<Geometry::Mesh> l_Meshes{};
            std::filesystem::path l_Path = Utilities::FileManagement::NormalizePath(filePath);

            std::string l_Ext = l_Path.extension().string();
            std::transform(l_Ext.begin(), l_Ext.end(), l_Ext.begin(), ::tolower);

            if (l_Ext == ".fbx")
            {
                Geometry::Mesh l_Mesh{};
                if (!LoadFBX(l_Path, l_Mesh))
                {
                    TR_CORE_CRITICAL("Failed to load FBX model: {}", filePath);
                }

                if (!l_Mesh.Vertices.empty())
                {
                    l_Meshes.push_back(std::move(l_Mesh));
                }

                return l_Meshes;
            }

            if (l_Ext == ".obj")
            {
                if (!LoadOBJ(l_Path, l_Meshes))
                {
                    TR_CORE_CRITICAL("Failed to load OBJ model: {}", filePath);
                }

                return l_Meshes;
            }

            if (l_Ext == ".gltf" || l_Ext == ".glb")
            {
                if (!LoadGLTF(l_Path, l_Meshes))
                {
                    TR_CORE_CRITICAL("Failed to load GLTF model: {}", filePath);
                }

                return l_Meshes;
            }

            TR_CORE_CRITICAL("Unsupported model format: {}", filePath);
         
            return l_Meshes;
        }
    }
}