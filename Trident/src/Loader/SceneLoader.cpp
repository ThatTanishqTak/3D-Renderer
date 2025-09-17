#include "Loader/SceneLoader.h"
#include "Loader/ModelLoader.h"
#include "Core/Utilities.h"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace Trident
{
    namespace Loader
    {
        SceneData SceneLoader::Load(const std::string& directoryPath)
        {
            SceneData l_Scene{};
            fs::path l_Path = Utilities::FileManagement::NormalizePath(directoryPath);
            if (!fs::is_directory(l_Path))
            {
                TR_CORE_ERROR("Scene path is not a directory: {}", directoryPath);
                return l_Scene;
            }

            for (const auto& a_Entry : fs::directory_iterator(l_Path))
            {
                if (!a_Entry.is_regular_file())
                    continue;

                auto a_Ext = a_Entry.path().extension();
                if (a_Ext == ".gltf" || a_Ext == ".glb")
                {
                    auto a_ModelData = ModelLoader::Load(a_Entry.path().string());
                    if (!a_ModelData.Meshes.empty())
                    {
                        const size_t l_MaterialOffset = l_Scene.Materials.size();
                        for (auto& l_Mesh : a_ModelData.Meshes)
                        {
                            if (l_Mesh.MaterialIndex >= 0)
                            {
                                l_Mesh.MaterialIndex += static_cast<int>(l_MaterialOffset);
                            }
                            l_Scene.Meshes.push_back(std::move(l_Mesh));
                        }

                        l_Scene.Materials.insert(
                            l_Scene.Materials.end(),
                            a_ModelData.Materials.begin(),
                            a_ModelData.Materials.end());
                        ++l_Scene.ModelCount;
                    }
                }
            }

            for (const auto& l_Mesh : l_Scene.Meshes)
            {
                l_Scene.TriangleCount += l_Mesh.Indices.size() / 3;
            }

            TR_CORE_INFO("Scene loaded: {} models, {} triangles", l_Scene.ModelCount, l_Scene.TriangleCount);

            return l_Scene;
        }
    }
}