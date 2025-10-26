#include "SceneHierarchyPanel.h"

#include "Events/MouseCodes.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Registry.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

void SceneHierarchyPanel::SetRegistry(Trident::ECS::Registry* registry)
{
    // Store the pointer so the hierarchy can consistently query the same registry regardless of play mode swaps.
    m_Registry = registry;
}

void SceneHierarchyPanel::Update()
{
    if (m_Registry == nullptr)
    {
        // Without a registry the panel has nothing meaningful to validate yet; the layer wires this up during initialise.
        return;
    }

    const std::vector<Trident::ECS::Entity>& l_Entities = m_Registry->GetEntities();

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

    if (m_Registry == nullptr)
    {
        // Guard against early renders before the application layer has assigned a registry pointer.
        ImGui::TextUnformatted("No registry assigned");
        ImGui::End();

        return;
    }

    const std::vector<Trident::ECS::Entity>& l_Entities = m_Registry->GetEntities();

    // Track focus/hover state so the hierarchy can offer a contextual creation menu when right-clicked.
    const bool l_WindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool l_WindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
    if (l_WindowFocused && l_WindowHovered && ImGui::IsMouseClicked(Trident::Mouse::ButtonRight))
    {
        ImGui::OpenPopup("SceneHierarchyContextMenu");
    }

    if (ImGui::BeginPopup("SceneHierarchyContextMenu"))
    {
        // Offer entity creation helpers when the owning application supplied callbacks.
        if (ImGui::MenuItem("Create Empty Entity", nullptr, false, static_cast<bool>(m_CreateEmptyEntityAction)))
        {
            m_CreateEmptyEntityAction();
        }

        const bool l_HasAnyPrimitiveCreator = static_cast<bool>(m_CreateCubePrimitiveAction) || static_cast<bool>(m_CreateSpherePrimitiveAction) ||
            static_cast<bool>(m_CreateQuadPrimitiveAction);
        if (ImGui::BeginMenu("Create Primitive", l_HasAnyPrimitiveCreator))
        {
            if (ImGui::MenuItem("Cube", nullptr, false, static_cast<bool>(m_CreateCubePrimitiveAction)))
            {
                m_CreateCubePrimitiveAction();
            }
            if (ImGui::MenuItem("Sphere", nullptr, false, static_cast<bool>(m_CreateSpherePrimitiveAction)))
            {
                m_CreateSpherePrimitiveAction();
            }
            if (ImGui::MenuItem("Quad", nullptr, false, static_cast<bool>(m_CreateQuadPrimitiveAction)))
            {
                m_CreateQuadPrimitiveAction();
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    for (Trident::ECS::Entity it_Entity : l_Entities)
    {
        DrawEntityNode(it_Entity, *m_Registry);
    }

    // Allow deselection by clicking an empty portion of the window.
    // Use engine mouse codes so hierarchy deselection honours the shared input layer.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(Trident::Mouse::ButtonLeft))
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

void SceneHierarchyPanel::SetContextMenuActions(std::function<void()> createEmptyEntityAction, std::function<void()> createCubeAction, std::function<void()> createSphereAction,
    std::function<void()> createQuadAction)
{
    // Cache the editor-provided callbacks so the context menu can invoke them later.
    m_CreateEmptyEntityAction = std::move(createEmptyEntityAction);
    m_CreateCubePrimitiveAction = std::move(createCubeAction);
    m_CreateSpherePrimitiveAction = std::move(createSphereAction);
    m_CreateQuadPrimitiveAction = std::move(createQuadAction);
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

    // Engine mouse codes keep selection interactions consistent with the rest of the editor.
    if (ImGui::IsItemClicked(Trident::Mouse::ButtonLeft))
    {
        // TODO: When the input module exposes shared helpers, route click and double-click handling through them.
        m_SelectedEntity = entity;
    }

    if (l_NodeOpen)
    {
        ImGui::TreePop();
    }

    // Future improvement: add context menus here for creating and deleting entities.
}