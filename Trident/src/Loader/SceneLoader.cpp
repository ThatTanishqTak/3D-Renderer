#include "Loader/SceneLoader.h"
#include "Loader/ModelLoader.h"
#include "Core/Utilities.h"

#include <filesystem>

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

            for (const auto& l_Entry : fs::directory_iterator(l_Path))
            {
                if (!l_Entry.is_regular_file())
                    continue;

                auto ext = l_Entry.path().extension();
                if (ext == ".gltf" || ext == ".glb")
                {
                    auto l_Meshes = ModelLoader::Load(l_Entry.path().string());
                    if (!l_Meshes.empty())
                    {
                        l_Scene.Meshes.insert(l_Scene.Meshes.end(), l_Meshes.begin(), l_Meshes.end());
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