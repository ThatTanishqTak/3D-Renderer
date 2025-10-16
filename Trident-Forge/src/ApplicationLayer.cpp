#include "ApplicationLayer.h"

#include "Application/Startup.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Loader/AssimpExtensions.h"
#include "Loader/ModelLoader.h"
#include "Renderer/RenderCommand.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cmath>
#include <iterator>

void ApplicationLayer::Initialize()
{
    // Wire up the gizmo state so the viewport and inspector remain in sync.
    m_ViewportPanel.SetGizmoState(&m_GizmoState);
    m_InspectorPanel.SetGizmoState(&m_GizmoState);

    // Route drag-and-drop payloads originating inside the editor back into the shared import path.
    m_ViewportPanel.SetAssetDropHandler([this](const std::vector<std::string>& droppedPaths)
        {
            ImportDroppedAssets(droppedPaths);
        });
}

void ApplicationLayer::Shutdown()
{

}

void ApplicationLayer::Update()
{
    m_ViewportPanel.Update();
    m_ContentBrowserPanel.Update();
    m_SceneHierarchyPanel.Update();

    // Push the hierarchy selection into the inspector before it performs validation.
    const Trident::ECS::Entity l_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
    m_InspectorPanel.SetSelectedEntity(l_SelectedEntity);
    m_InspectorPanel.Update();
}

void ApplicationLayer::Render()
{
    m_ViewportPanel.Render();
    m_ContentBrowserPanel.Render();
    m_SceneHierarchyPanel.Render();
    m_InspectorPanel.Render();
}

void ApplicationLayer::OnEvent(Trident::Events& event)
{
    Trident::EventDispatcher l_Dispatcher(event);
    l_Dispatcher.Dispatch<Trident::FileDropEvent>([this](Trident::FileDropEvent& dropEvent)
        {
            return HandleFileDrop(dropEvent);
        });
}

bool ApplicationLayer::HandleFileDrop(Trident::FileDropEvent& event)
{
    ImGuiIO& l_IO = ImGui::GetIO();
    const bool l_HasMousePosition = std::isfinite(l_IO.MousePos.x) && std::isfinite(l_IO.MousePos.y);
    const bool l_IsWithinViewport = l_HasMousePosition && m_ViewportPanel.ContainsPoint(l_IO.MousePos);

    if (!m_ViewportPanel.IsHovered() && !l_IsWithinViewport)
    {
        // Ignore drops that land outside the viewport so accidental drags do not spawn entities.
        return false;
    }
    return ImportDroppedAssets(event.GetPaths());
}

bool ApplicationLayer::ImportDroppedAssets(const std::vector<std::string>& droppedPaths)
{
    if (droppedPaths.empty())
    {
        return false;
    }

    const std::vector<std::string>& l_SupportedExtensions = Trident::Loader::AssimpExtensions::GetNormalizedExtensions();
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();

    // Cache the current mesh count so new entities can reference the appended geometry correctly.
    const size_t l_InitialMeshCount = Trident::RenderCommand::GetModelCount();

    std::vector<Trident::Geometry::Mesh> l_ImportedMeshes{};
    std::vector<Trident::Geometry::Material> l_ImportedMaterials{};
    bool l_ImportedAny = false;

    for (const std::string& it_Path : droppedPaths)
    {
        std::filesystem::path l_PathView{ it_Path };
        std::string l_Extension = l_PathView.extension().string();
        std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char a_Char)
            {
                return static_cast<char>(std::tolower(a_Char));
            });

        const bool l_Supported = std::find(l_SupportedExtensions.begin(), l_SupportedExtensions.end(), l_Extension) != l_SupportedExtensions.end();
        if (!l_Supported)
        {
            continue;
        }

        Trident::Loader::ModelData l_ModelData = Trident::Loader::ModelLoader::Load(it_Path);
        if (l_ModelData.Meshes.empty())
        {
            continue;
        }

        const std::string l_BaseName = l_PathView.stem().string();
        const std::string l_TagRoot = l_BaseName.empty() ? std::string("Imported Mesh") : l_BaseName;

        for (size_t it_MeshIndex = 0; it_MeshIndex < l_ModelData.Meshes.size(); ++it_MeshIndex)
        {
            Trident::Geometry::Mesh& l_Mesh = l_ModelData.Meshes[it_MeshIndex];

            // Preserve the mesh data so the renderer can rebuild GPU buffers after all drops are processed.
            const size_t l_PreviousMeshCount = l_ImportedMeshes.size();
            l_ImportedMeshes.emplace_back(std::move(l_Mesh));
            const size_t l_AssignedMeshIndex = l_InitialMeshCount + l_PreviousMeshCount;

            Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();
            // Default transform keeps the asset centred at the origin with unit scale so artists can position it manually.
            l_Registry.AddComponent<Trident::Transform>(l_NewEntity, Trident::Transform{});

            Trident::MeshComponent& l_MeshComponent = l_Registry.AddComponent<Trident::MeshComponent>(l_NewEntity);
            l_MeshComponent.m_MeshIndex = l_AssignedMeshIndex;
            l_MeshComponent.m_Visible = true;

            Trident::TagComponent& l_TagComponent = l_Registry.AddComponent<Trident::TagComponent>(l_NewEntity);
            l_TagComponent.m_Tag = l_TagRoot;
            if (l_ModelData.Meshes.size() > 1)
            {
                l_TagComponent.m_Tag += " (" + std::to_string(it_MeshIndex + 1) + ")";
            }

            l_ImportedAny = true;
        }

        // Transfer materials after entities so the renderer can align indices when rebuilding draw buffers.
        std::move(l_ModelData.Materials.begin(), l_ModelData.Materials.end(), std::back_inserter(l_ImportedMaterials));
    }

    if (!l_ImportedAny || l_ImportedMeshes.empty())
    {
        return false;
    }

    // Ask the renderer to append the new meshes so existing GPU resources stay valid and the ECS draw metadata stays synced.
    Trident::RenderCommand::AppendMeshes(std::move(l_ImportedMeshes), std::move(l_ImportedMaterials));

    return true;
}