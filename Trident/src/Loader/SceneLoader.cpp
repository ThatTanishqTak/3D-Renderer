#include "Loader/SceneLoader.h"
#include "Loader/ModelLoader.h"
#include "Loader/AssimpExtensions.h"
#include "Core/Utilities.h"
#include "Application/Startup.h"

#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TransformComponent.h"

#include <filesystem>
#include <utility>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace Trident
{
    namespace Loader
    {
        SceneData SceneLoader::Load(const std::string& directoryPath)
        {
            SceneData l_Scene{};

            // Resolve the active registry up front so meshes can spawn ECS entities as they are imported.
            ECS::Registry& l_Registry = Startup::GetRegistry();
            fs::path l_Path = Utilities::FileManagement::NormalizePath(directoryPath);
            if (!fs::is_directory(l_Path))
            {
                TR_CORE_ERROR("Scene path is not a directory: {}", directoryPath);
                return l_Scene;
            }

            const std::vector<std::string>& l_SupportedExtensions = AssimpExtensions::GetNormalizedExtensions();
            // Assimp exposes its supported formats at runtime, letting us accept any importer-capable asset.
            // TODO: Consider caching the directory scan results so repeated scene loads avoid redundant disk hits.

            for (const auto& a_Entry : fs::directory_iterator(l_Path))
            {
                if (!a_Entry.is_regular_file())
                {
                    continue;
                }

                std::string l_Extension = a_Entry.path().extension().string();
                std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char a_Char)
                    {
                        return static_cast<char>(std::tolower(a_Char));
                    });

                const bool l_IsAssimpSupported = std::find(l_SupportedExtensions.begin(), l_SupportedExtensions.end(), l_Extension) != l_SupportedExtensions.end();
                // The directory scan now trusts Assimp's advertised extensions instead of a hard-coded allowlist.
                if (l_IsAssimpSupported)
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
                            const size_t l_NewMeshIndex = l_Scene.Meshes.size();
                            l_Scene.Meshes.push_back(std::move(l_Mesh));

                            // Create an entity for the imported mesh so the renderer drives draw calls via ECS data.
                            ECS::Entity l_Entity = l_Registry.CreateEntity();
                            // Start with an identity transform; editor tools can adjust this later.
                            l_Registry.AddComponent<Transform>(l_Entity, Transform{});
                            MeshComponent& l_MeshComponent = l_Registry.AddComponent<MeshComponent>(l_Entity);
                            l_MeshComponent.m_MeshIndex = l_NewMeshIndex;
                            l_MeshComponent.m_MaterialIndex = l_Scene.Meshes.back().MaterialIndex;
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