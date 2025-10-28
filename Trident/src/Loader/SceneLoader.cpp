#include "Loader/SceneLoader.h"
#include "Loader/ModelLoader.h"
#include "Loader/AssimpExtensions.h"
#include "Core/Utilities.h"
#include "Application/Startup.h"

#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/TagComponent.h"

#include <filesystem>
#include <utility>
#include <algorithm>
#include <cctype>
#include <limits>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

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

            const auto DecomposeMatrixToTransform = [&](const glm::mat4& a_ModelMatrix) -> Transform
                {
                    Transform l_Result{};
                    glm::vec3 l_Scale{ 1.0f };
                    glm::quat l_Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
                    glm::vec3 l_Translation{ 0.0f };
                    glm::vec3 l_Skew{ 0.0f };
                    glm::vec4 l_Perspective{ 0.0f };

                    if (glm::decompose(a_ModelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
                    {
                        l_Rotation = glm::normalize(l_Rotation);
                        l_Result.Position = l_Translation;
                        l_Result.Scale = l_Scale;
                        l_Result.Rotation = glm::degrees(glm::eulerAngles(l_Rotation));
                    }
                    else
                    {
                        TR_CORE_WARN("Failed to decompose transform matrix while importing scene");
                    }

                    return l_Result;
                };

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
                    if (l_IsAssimpSupported)
                    {
                        auto a_ModelData = ModelLoader::Load(a_Entry.path().string());
                        if (a_ModelData.m_Meshes.empty() && a_ModelData.m_MeshInstances.empty())
                        {
                            continue;
                        }

                        const size_t l_MaterialOffset = l_Scene.Materials.size();
                        const size_t l_MeshOffset = l_Scene.Meshes.size();

                        for (size_t it_Mesh = 0; it_Mesh < a_ModelData.m_Meshes.size(); ++it_Mesh)
                        {
                            Geometry::Mesh l_Mesh = std::move(a_ModelData.m_Meshes[it_Mesh]);
                            if (l_Mesh.MaterialIndex >= 0)
                            {
                                l_Mesh.MaterialIndex += static_cast<int>(l_MaterialOffset);
                            }
                            l_Scene.Meshes.push_back(std::move(l_Mesh));
                        }

                        const auto SpawnMeshEntity = [&](size_t a_MeshIndex, const glm::mat4& a_ModelMatrix, const std::string& a_NodeName)
                            {
                                ECS::Entity l_Entity = l_Registry.CreateEntity();

                                // Decompose the composed transform so runtime systems can work with translation/rotation/scale directly.
                                const Transform l_Decomposed = DecomposeMatrixToTransform(a_ModelMatrix);
                                l_Registry.AddComponent<Transform>(l_Entity, l_Decomposed);

                                MeshComponent& l_MeshComponent = l_Registry.AddComponent<MeshComponent>(l_Entity);
                                l_MeshComponent.m_MeshIndex = a_MeshIndex;
                                l_MeshComponent.m_MaterialIndex = (a_MeshIndex < l_Scene.Meshes.size())
                                    ? l_Scene.Meshes[a_MeshIndex].MaterialIndex
                                    : -1;
                                l_MeshComponent.m_Primitive = MeshComponent::PrimitiveType::None;

                                if (!a_NodeName.empty())
                                {
                                    TagComponent& l_Tag = l_Registry.AddComponent<TagComponent>(l_Entity);
                                    l_Tag.m_Tag = a_NodeName;
                                }
                            };

                        if (!a_ModelData.m_MeshInstances.empty())
                        {
                            for (const MeshInstance& l_Instance : a_ModelData.m_MeshInstances)
                            {
                                if (l_Instance.m_MeshIndex == std::numeric_limits<size_t>::max())
                                {
                                    continue;
                                }

                                const size_t l_GlobalMeshIndex = l_MeshOffset + l_Instance.m_MeshIndex;
                                if (l_GlobalMeshIndex >= l_Scene.Meshes.size())
                                {
                                    TR_CORE_WARN("Mesh instance '{}' references invalid mesh {}", l_Instance.m_NodeName.c_str(), l_GlobalMeshIndex);
                                    continue;
                                }

                                SpawnMeshEntity(l_GlobalMeshIndex, l_Instance.m_ModelMatrix, l_Instance.m_NodeName);
                            }
                        }
                        else
                        {
                            // Legacy fallback: create identity transforms when the model did not provide per-node placements.
                            for (size_t it_Mesh = 0; it_Mesh < a_ModelData.m_Meshes.size(); ++it_Mesh)
                            {
                                const size_t l_GlobalMeshIndex = l_MeshOffset + it_Mesh;
                                SpawnMeshEntity(l_GlobalMeshIndex, glm::mat4(1.0f), {});
                            }
                        }

                        l_Scene.Materials.insert(l_Scene.Materials.end(), a_ModelData.m_Materials.begin(), a_ModelData.m_Materials.end());
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