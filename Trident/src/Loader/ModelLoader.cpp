#include "Loader/ModelLoader.h"
#include "Core/Utilities.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <libdeflate.c>

#include "ofbx.h"

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            static bool LoadFBX(const std::filesystem::path& path, Geometry::Mesh& mesh)
            {
                std::ifstream l_File(path, std::ios::binary | std::ios::ate);
                if (!l_File.is_open())
                {
                    TR_CORE_ERROR("Failed to open FBX file: {}", path.string());
                    return false;
                }

                const size_t l_Size = static_cast<size_t>(l_File.tellg());
                l_File.seekg(0);
                std::vector<ofbx::u8> l_Data(l_Size);
                l_File.read(reinterpret_cast<char*>(l_Data.data()), static_cast<std::streamsize>(l_Size));

                ofbx::IScene* l_Scene = ofbx::load(l_Data.data(), l_Size, static_cast<u16>(ofbx::LoadFlags::NONE));
                if (!l_Scene)
                {
                    TR_CORE_ERROR("Failed to parse FBX: {}", ofbx::getError());
                    return false;
                }

                if (l_Scene->getGeometryCount() == 0)
                {
                    l_Scene->destroy();
                    return false;
                }

                const ofbx::Geometry* l_Geometry = l_Scene->getGeometry(0);
                const ofbx::GeometryData& l_DataGeom = l_Geometry->getGeometryData();

                auto l_Pos = l_DataGeom.getPositions();
                mesh.Vertices.clear();
                mesh.Vertices.reserve(static_cast<size_t>(l_Pos.count));
                for (int i = 0; i < l_Pos.count; ++i)
                {
                    ofbx::Vec3 l_V = l_Pos.get(i);
                    Vertex l_Vertex{};
                    l_Vertex.Position = { l_V.x, l_V.y, l_V.z };
                    l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                    mesh.Vertices.push_back(l_Vertex);
                }

                mesh.Indices.clear();
                for (int p = 0; p < l_DataGeom.getPartitionCount(); ++p)
                {
                    auto l_Part = l_DataGeom.getPartition(p);
                    std::vector<int> l_Tri(static_cast<size_t>(l_Part.max_polygon_triangles * 3));
                    for (int i = 0; i < l_Part.polygon_count; ++i)
                    {
                        int l_Count = ofbx::triangulate(l_DataGeom, l_Part.polygons[i], l_Tri.data());
                        for (int t = 0; t < l_Count; ++t)
                        {
                            mesh.Indices.push_back(static_cast<uint32_t>(l_Tri[t]));
                        }
                    }
                }

                l_Scene->destroy();
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