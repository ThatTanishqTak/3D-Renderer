#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

namespace Trident
{
    namespace Loader
    {
        static std::string GetSection(const std::string& content, const std::string& marker)
        {
            size_t l_Start = content.find(marker);
            if (l_Start == std::string::npos)
            {
                return {};
            }

            l_Start = content.find('{', l_Start);
            if (l_Start == std::string::npos)
            {
                return {};
            }

            size_t l_End = content.find('}', l_Start);
            if (l_End == std::string::npos)
            {
                return {};
            }

            return content.substr(l_Start + 1, l_End - l_Start - 1);
        }

        static std::vector<float> ParseFloatList(const std::string& data)
        {
            std::vector<float> l_Result;
            l_Result.reserve(std::count(data.begin(), data.end(), ',') + 1);

            std::stringstream l_Stream(data);
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

        static std::vector<int> ParseIntList(const std::string& data)
        {
            std::vector<int> l_Result;
            l_Result.reserve(std::count(data.begin(), data.end(), ',') + 1);

            std::stringstream l_Stream(data);
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

        static bool ParseFBX(const std::string& path, Geometry::Mesh& mesh)
        {
            std::ifstream l_File(path, std::ios::in | std::ios::binary);
            if (!l_File.is_open())
            {
                TR_CORE_ERROR("Failed to open FBX file: {}", path);

                return false;
            }

            std::string l_Content((std::istreambuf_iterator<char>(l_File)), std::istreambuf_iterator<char>());

            std::string l_VertData = GetSection(l_Content, "Vertices:");
            std::string l_IndexData = GetSection(l_Content, "PolygonVertexIndex:");

            if (l_VertData.empty() || l_IndexData.empty())
            {
                TR_CORE_CRITICAL("Incomplete FBX data in {}", path);

                return false;
            }

            auto l_Vertices = ParseFloatList(l_VertData);
            mesh.Vertices.reserve(l_Vertices.size() / 3);
            for (size_t i = 0; i + 2 < l_Vertices.size(); i += 3)
            {
                Vertex l_Vertex{};

                l_Vertex.Position = { l_Vertices[i], l_Vertices[i + 1], l_Vertices[i + 2] };
                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                mesh.Vertices.push_back(l_Vertex);
            }

            auto l_Indices = ParseIntList(l_IndexData);
            mesh.Indices.reserve(l_Indices.size());
            std::vector<uint16_t> l_Polygon;
            for (int l_Value : l_Indices)
            {
                bool l_End = l_Value < 0;
                uint16_t l_Index = static_cast<uint16_t>(l_End ? ~l_Value : l_Value);
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

        static bool ParseOBJ(const std::string& path, Geometry::Mesh& mesh)
        {
            std::ifstream l_File(path, std::ios::in | std::ios::binary);
            if (!l_File.is_open())
            {
                TR_CORE_ERROR("Failed to open OBJ file: {}", path);

                return false;
            }

            std::vector<std::string> l_Lines;
            std::string l_Line;
            while (std::getline(l_File, l_Line))
            {
                l_Lines.push_back(l_Line);
            }

            size_t l_PosCount = 0;
            size_t l_FaceCount = 0;
            for (const auto& l_RawLine : l_Lines)
            {
                if (l_RawLine.rfind("v ", 0) == 0)
                {
                    ++l_PosCount;
                }
                else if (l_RawLine.rfind("f ", 0) == 0)
                {
                    ++l_FaceCount;
                }
            }

            std::vector<glm::vec3> l_Positions;
            l_Positions.reserve(l_PosCount);
            std::vector<glm::vec2> l_Texcoords;
            l_Texcoords.reserve(l_PosCount);
            std::unordered_map<std::string, uint16_t> l_Unique;

            mesh.Vertices.reserve(l_PosCount);
            mesh.Indices.reserve(l_FaceCount * 3);

            for (const auto& l_RawLine : l_Lines)
            {
                std::stringstream l_Stream(l_RawLine);
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
                    glm::vec2 l_Tex;
                    l_Stream >> l_Tex.x >> l_Tex.y;
                    l_Texcoords.push_back(l_Tex);
                }

                else if (l_Prefix == "f")
                {
                    std::vector<uint16_t> l_Face;
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
                                glm::vec2 l_Tex = l_Texcoords[l_TexIndex - 1];
                                l_Vertex.TexCoord = { l_Tex.x, 1.0f - l_Tex.y };
                            }

                            uint16_t l_Index = static_cast<uint16_t>(mesh.Vertices.size());
                            mesh.Vertices.push_back(l_Vertex);
                            l_Unique[l_VertexData] = l_Index;
                            l_Face.push_back(l_Index);
                        }
                        else
                        {
                            l_Face.push_back(l_It->second);
                        }
                    }

                    for (size_t i = 1; i + 1 < l_Face.size(); ++i)
                    {
                        mesh.Indices.push_back(l_Face[0]);
                        mesh.Indices.push_back(l_Face[i]);
                        mesh.Indices.push_back(l_Face[i + 1]);
                    }
                }
            }

            return !mesh.Vertices.empty();
        }

        Geometry::Mesh ModelLoader::Load(const std::string& filePath)
        {
            Geometry::Mesh l_Mesh{};

            tinygltf::TinyGLTF l_Loader;
            tinygltf::Model l_Model{};
            std::string l_Err;
            std::string l_Warn;

            bool l_Binary = false;
            std::string l_Ext = Utilities::FileManagement::GetExtension(filePath);
            std::transform(l_Ext.begin(), l_Ext.end(), l_Ext.begin(), ::tolower);

            if (l_Ext == ".fbx")
            {
                if (!ParseFBX(filePath, l_Mesh))
                {
                    TR_CORE_CRITICAL("Failed to load FBX model: {}", filePath);
                }

                return l_Mesh;
            }

            if (l_Ext == ".obj")
            {
                if (!ParseOBJ(filePath, l_Mesh))
                {
                    TR_CORE_CRITICAL("Failed to load OBJ model: {}", filePath);
                }

                return l_Mesh;
            }

            l_Binary = l_Ext == ".glb";

            bool l_Loaded = false;
            if (l_Binary)
            {
                l_Loaded = l_Loader.LoadBinaryFromFile(&l_Model, &l_Err, &l_Warn, filePath);
            }
            else
            {
                l_Loaded = l_Loader.LoadASCIIFromFile(&l_Model, &l_Err, &l_Warn, filePath);
            }

            if (!l_Warn.empty())
            {
                TR_CORE_WARN("{}", l_Warn);
            }

            if (!l_Loaded)
            {
                TR_CORE_CRITICAL("Failed to load model: {} ({})", filePath, l_Err);

                return l_Mesh;
            }

            if (l_Model.meshes.empty())
            {
                TR_CORE_WARN("No mesh found in {}", filePath);
                
                return l_Mesh;
            }

            if (l_Model.meshes.empty())
            {
                TR_CORE_WARN("No mesh found in {}", filePath);

                return l_Mesh;
            }

            const tinygltf::Mesh& l_FirstMesh = l_Model.meshes[0];
            if (l_FirstMesh.primitives.empty())
            {
                TR_CORE_WARN("No primitives in {}", l_FirstMesh.name);
                
                return l_Mesh;
            }

            const tinygltf::Primitive& l_Prim = l_FirstMesh.primitives[0];
            auto l_PosIt = l_Prim.attributes.find("POSITION");
            if (l_PosIt == l_Prim.attributes.end())
            {
                TR_CORE_CRITICAL("Primitive has no POSITION attribute");
                
                return l_Mesh;
            }

            const tinygltf::Accessor& l_PosAcc = l_Model.accessors[l_PosIt->second];
            const tinygltf::BufferView& l_PosView = l_Model.bufferViews[l_PosAcc.bufferView];
            const tinygltf::Buffer& l_PosBuf = l_Model.buffers[l_PosView.buffer];
            const float* l_Positions = reinterpret_cast<const float*>(&l_PosBuf.data[l_PosView.byteOffset + l_PosAcc.byteOffset]);

            l_Mesh.Vertices.reserve(l_PosAcc.count);

            const float* l_Texcoords = nullptr;
            bool l_HasTexcoord = false;
            auto l_TexIt = l_Prim.attributes.find("TEXCOORD_0");
            if (l_TexIt != l_Prim.attributes.end())
            {
                const tinygltf::Accessor& l_TexAcc = l_Model.accessors[l_TexIt->second];
                const tinygltf::BufferView& l_TexView = l_Model.bufferViews[l_TexAcc.bufferView];
                const tinygltf::Buffer& l_TexBuf = l_Model.buffers[l_TexView.buffer];
                
                l_Texcoords = reinterpret_cast<const float*>(&l_TexBuf.data[l_TexView.byteOffset + l_TexAcc.byteOffset]);
                l_HasTexcoord = true;
            }

            for (size_t i = 0; i < l_PosAcc.count; ++i)
            {
                Vertex l_Vertex{};
                l_Vertex.Position = { l_Positions[i * 3 + 0], l_Positions[i * 3 + 1], l_Positions[i * 3 + 2] };
                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                if (l_HasTexcoord)
                {
                    l_Vertex.TexCoord = { l_Texcoords[i * 2 + 0], 1.0f - l_Texcoords[i * 2 + 1] };
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
                    l_Mesh.Indices.insert(l_Mesh.Indices.end(), l_Indices, l_Indices + l_IndAcc.count);
                }

                else if (l_IndAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
                {
                    const uint32_t* l_Indices = reinterpret_cast<const uint32_t*>(&l_IndBuf.data[l_IndView.byteOffset + l_IndAcc.byteOffset]);
                    for (size_t i = 0; i < l_IndAcc.count; ++i)
                    {
                        l_Mesh.Indices.push_back(static_cast<uint16_t>(l_Indices[i]));
                    }
                }
            }

            return l_Mesh;
        }
    }
}