#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <fstream>
#include <sstream>
#include <algorithm>

namespace Trident
{
    namespace Loader
    {
        static std::string GetSection(const std::string& a_Content, const std::string& a_Marker)
        {
            size_t l_Start = a_Content.find(a_Marker);
            if (l_Start == std::string::npos)
            {
                return {};
            }

            l_Start = a_Content.find('{', l_Start);
            if (l_Start == std::string::npos)
            {
                return {};
            }
            size_t l_End = a_Content.find('}', l_Start);
            if (l_End == std::string::npos)
            {
                return {};
            }

            return a_Content.substr(l_Start + 1, l_End - l_Start - 1);
        }

        static std::vector<float> ParseFloatList(const std::string& a_Data)
        {
            std::vector<float> l_Result;
            std::stringstream l_Stream(a_Data);
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

        static std::vector<int> ParseIntList(const std::string& a_Data)
        {
            std::vector<int> l_Result;
            std::stringstream l_Stream(a_Data);
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

        static bool ParseFBX(const std::string& a_Path, Geometry::Mesh& a_Mesh)
        {
            std::ifstream l_File(a_Path);
            if (!l_File.is_open())
            {
                TR_CORE_ERROR("Failed to open FBX file: {}", a_Path);

                return false;
            }

            std::string l_Content((std::istreambuf_iterator<char>(l_File)), std::istreambuf_iterator<char>());

            std::string l_VertData = GetSection(l_Content, "Vertices:");
            std::string l_IndexData = GetSection(l_Content, "PolygonVertexIndex:");

            if (l_VertData.empty() || l_IndexData.empty())
            {
                TR_CORE_CRITICAL("Incomplete FBX data in {}", a_Path);

                return false;
            }

            auto l_Vertices = ParseFloatList(l_VertData);
            for (size_t i = 0; i + 2 < l_Vertices.size(); i += 3)
            {
                Vertex l_Vertex{};
                l_Vertex.Position = { l_Vertices[i], l_Vertices[i + 1], l_Vertices[i + 2] };
                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                a_Mesh.Vertices.push_back(l_Vertex);
            }

            auto l_Indices = ParseIntList(l_IndexData);
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
                        a_Mesh.Indices.push_back(l_Polygon[0]);
                        a_Mesh.Indices.push_back(l_Polygon[i]);
                        a_Mesh.Indices.push_back(l_Polygon[i + 1]);
                    }
                    l_Polygon.clear();
                }
            }

            return !a_Mesh.Vertices.empty();
        }
        Geometry::Mesh ModelLoader::Load(const std::string& filePath)
        {
            Geometry::Mesh l_Mesh{};

            tinygltf::TinyGLTF l_Loader;
            tinygltf::Model l_Model{};
            std::string l_Err;
            std::string l_Warn;

            bool l_Binary = false;
            std::string l_Ext;
            if (filePath.size() > 4)
            {
                l_Ext = filePath.substr(filePath.size() - 4);
                std::transform(l_Ext.begin(), l_Ext.end(), l_Ext.begin(), ::tolower);
            }

            if (l_Ext != ".gltf" && l_Ext != ".glb" && l_Ext != ".fbx")
            {
                TR_CORE_CRITICAL("Unsupported model format: {}", filePath);

                return l_Mesh;
            }

            if (l_Ext == ".fbx")
            {
                if (!ParseFBX(filePath, l_Mesh))
                {
                    TR_CORE_CRITICAL("Failed to load FBX model: {}", filePath);
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