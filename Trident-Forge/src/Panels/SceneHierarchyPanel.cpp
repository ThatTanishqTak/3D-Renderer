#include "SceneHierarchyPanel.h"

#include "Application/Startup.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Registry.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

void SceneHierarchyPanel::Update()
{
    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();

    if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
    {
        return;
    }

    const bool l_SelectionStillExists = std::find(l_Entities.begin(), l_Entities.end(), m_SelectedEntity) != l_Entities.end();
    if (!l_SelectionStillExists)
    {
        // Clear the cached selection so the inspector does not reference freed components.
        m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    }
}

void SceneHierarchyPanel::Render()
{
    if (!ImGui::Begin("Scene Hierarchy"))
    {
        ImGui::End();
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();

    for (Trident::ECS::Entity it_Entity : l_Entities)
    {
        DrawEntityNode(it_Entity, l_Registry);
    }

    // Allow deselection by clicking an empty portion of the window.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const bool l_ClickedItem = ImGui::IsAnyItemHovered();
        if (!l_ClickedItem)
        {
            m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
        }
    }

    ImGui::End();
}

Trident::ECS::Entity SceneHierarchyPanel::GetSelectedEntity() const
{
    return m_SelectedEntity;
}

void SceneHierarchyPanel::DrawEntityNode(Trident::ECS::Entity entity, Trident::ECS::Registry& registry)
{
    std::string l_DisplayName = "Entity " + std::to_string(entity);
    if (registry.HasComponent<Trident::TagComponent>(entity))
    {
        const Trident::TagComponent& l_TagComponent = registry.GetComponent<Trident::TagComponent>(entity);
        if (!l_TagComponent.m_Tag.empty())
        {
            l_DisplayName = l_TagComponent.m_Tag;
        }
    }

    ImGuiTreeNodeFlags l_NodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;
    if (entity == m_SelectedEntity)
    {
        l_NodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool l_NodeOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entity)), l_NodeFlags, "%s", l_DisplayName.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        m_SelectedEntity = entity;
    }

    if (l_NodeOpen)
    {
        ImGui::TreePop();
    }

    // Future improvement: add context menus here for creating and deleting entities.
}