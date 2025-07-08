#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

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
                std::vector<float> l_Result{};
                std::stringstream l_Stream{ std::string(data) };
                std::string l_Token{};
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
                std::vector<int> l_Result{};
                std::stringstream l_Stream{ std::string(data) };
                std::string l_Token{};
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
                std::string l_Verts = ExtractSection(l_Content, "Vertices:");
                std::string l_Indices = ExtractSection(l_Content, "PolygonVertexIndex:");
                if (l_Verts.empty() || l_Indices.empty())
                {
                    return false;
                }

                auto l_Pos = ParseFloats(l_Verts);
                auto l_Ind = ParseInts(l_Indices);

                mesh.Vertices.clear();
                mesh.Indices.clear();

                for (size_t i = 0; i + 2 < l_Pos.size(); i += 3)
                {
                    Vertex l_Vertex{};
                    l_Vertex.Position = { l_Pos[i], l_Pos[i + 1], l_Pos[i + 2] };
                    l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                    
                    mesh.Vertices.push_back(l_Vertex);
                }

                std::vector<uint32_t> l_Polygon{};
                for (int l_Value : l_Ind)
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
        }

        std::vector<Geometry::Mesh> ModelLoader::Load(const std::string& filePath)
        {
            std::vector<Geometry::Mesh> l_Meshes{};
            std::filesystem::path l_Path = Utilities::FileManagement::NormalizePath(filePath);

            if (l_Path.extension() != ".fbx")
            {
                TR_CORE_CRITICAL("Unsupported model format: {}", filePath);
                
                return l_Meshes;
            }

            Geometry::Mesh l_Mesh{};
            if (!LoadFBX(l_Path, l_Mesh))
            {
                TR_CORE_CRITICAL("Failed to load FBX model: {}", filePath);
            }
            else if (!l_Mesh.Vertices.empty())
            {
                l_Meshes.push_back(std::move(l_Mesh));
            }

            return l_Meshes;
        }
    }
}