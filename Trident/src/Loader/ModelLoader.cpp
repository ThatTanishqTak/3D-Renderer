#include "Loader/ModelLoader.h"

#include "Core/Utilities.h"

#include <tiny_obj_loader.h>
#include <unordered_map>

namespace Trident
{
    namespace Loader
    {
        Geometry::Mesh ModelLoader::LoadOBJ(const std::string& filePath)
        {
            Geometry::Mesh l_Mesh{};

            tinyobj::attrib_t l_Attrib;
            
            std::vector<tinyobj::shape_t> l_Shapes;
            std::vector<tinyobj::material_t> l_Materials;
            
            std::string l_Warn;
            std::string l_Err;

            std::string l_Normalized = Utilities::FileManagement::NormalizePath(filePath);
            std::string l_BaseDir = Utilities::FileManagement::GetBaseDirectory(l_Normalized);
            bool l_Loaded = tinyobj::LoadObj(&l_Attrib, &l_Shapes, &l_Materials, &l_Warn, &l_Err, l_Normalized.c_str(), l_BaseDir.c_str(), true);
            
            if (!l_Warn.empty())
            {
                TR_CORE_WARN("{}", l_Warn);
            }

            if (!l_Err.empty())
            {
                TR_CORE_ERROR("{}", l_Err);
            }

            if (!l_Loaded)
            {
                TR_CORE_CRITICAL("Failed to load model: {}", filePath);
                
                return l_Mesh;
            }

            struct Key
            {
                int PosIndex;
                int TexIndex;
                int NormIndex;
                bool operator==(const Key& other) const
                {
                    return PosIndex == other.PosIndex && TexIndex == other.TexIndex && NormIndex == other.NormIndex;
                }
            };

            struct KeyHasher
            {
                std::size_t operator()(const Key& k) const noexcept
                {
                    return std::hash<int>()(k.PosIndex) ^ (std::hash<int>()(k.TexIndex) << 1) ^ (std::hash<int>()(k.NormIndex) << 2);
                }
            };
            std::unordered_map<Key, uint16_t, KeyHasher> l_UniqueVertices;

            for (const auto& it_Shape : l_Shapes)
            {
                for (const auto& l_Index : it_Shape.mesh.indices)
                {
                    Key l_Key{ l_Index.vertex_index, l_Index.texcoord_index, l_Index.normal_index };
                    auto l_It = l_UniqueVertices.find(l_Key);
                    
                    if (l_It == l_UniqueVertices.end())
                    {
                        Vertex l_Vertex{};
                        l_Vertex.Position =
                        {
                            l_Attrib.vertices[3 * l_Index.vertex_index + 0],
                            l_Attrib.vertices[3 * l_Index.vertex_index + 1],
                            l_Attrib.vertices[3 * l_Index.vertex_index + 2] 
                        };

                        if (l_Index.texcoord_index >= 0)
                        {
                            l_Vertex.TexCoord =
                            {
                                l_Attrib.texcoords[2 * l_Index.texcoord_index + 0],
                                1.0f - l_Attrib.texcoords[2 * l_Index.texcoord_index + 1] 
                            };
                        }

                        l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                        uint16_t l_NewIndex = static_cast<uint16_t>(l_Mesh.Vertices.size());
                        l_UniqueVertices[l_Key] = l_NewIndex;
                        l_Mesh.Vertices.push_back(l_Vertex);
                        l_Mesh.Indices.push_back(l_NewIndex);
                    }
                    else
                    {
                        l_Mesh.Indices.push_back(l_It->second);
                    }
                }
            }

            return l_Mesh;
        }
    }
}