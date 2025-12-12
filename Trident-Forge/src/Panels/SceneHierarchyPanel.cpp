#include "SceneHierarchyPanel.h"

#include <imgui.h>
#include <string>

#include "ECS/Components/TagComponent.h"
#include "ECS/Components/UUIDComponent.h"

namespace EditorPanels
{
    void SceneHierarchyPanel::Update()
    {
        // Keep the status string aligned with registry availability for clear user feedback.
        if (m_Registry == nullptr || m_Registry->GetEntities().empty())
        {
            m_StatusMessage = "No entities registered";
        }
        else
        {
            m_StatusMessage = "Entities available";
        }
    }

    void SceneHierarchyPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Scene Hierarchy");
        (void)l_WindowVisible;
        // Always submit hierarchy content so dockspace layouts see the node regardless of collapse state.

        ImGui::TextWrapped(m_StatusMessage.c_str());

        if (m_Registry != nullptr)
        {
            for (Trident::ECS::Entity it_Entity : m_Registry->GetEntities())
            {
                // Track selection using an explicit sentinel so entity ID 0 remains selectable (e.g. default camera).
                const bool l_IsSelected = m_SelectedEntity != s_InvalidEntity && it_Entity == m_SelectedEntity;

                // Apply a stable ImGui ID so entities with identical display names remain selectable without conflicts.
                std::string l_NodeId = std::to_string(it_Entity);
                if (m_Registry->HasComponent<Trident::UUIDComponent>(it_Entity))
                {
                    const Trident::UUIDComponent& l_UUIDComponent = m_Registry->GetComponent<Trident::UUIDComponent>(it_Entity);
                    l_NodeId = std::to_string(l_UUIDComponent.m_ID.GetValue());
                }

                std::string l_Label;
                if (m_Registry->HasComponent<Trident::TagComponent>(it_Entity))
                {
                    const auto& l_Tag = m_Registry->GetComponent<Trident::TagComponent>(it_Entity);
                    if (!l_Tag.m_Tag.empty())
                    {
                        l_Label = l_Tag.m_Tag;
                    }
                }

                if (l_Label.empty())
                {
                    l_Label = "Entity " + std::to_string(it_Entity);
                }

                ImGui::PushID(l_NodeId.c_str());
                if (ImGui::Selectable(l_Label.c_str(), l_IsSelected))
                {
                    m_SelectedEntity = it_Entity;
                }
                ImGui::PopID();
            }
        }

        if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Empty"))
            {
                if (m_OnCreateEmpty)
                {
                    m_OnCreateEmpty();
                }
            }

            if (ImGui::MenuItem("Create Cube"))
            {
                if (m_OnCreateCube)
                {
                    m_OnCreateCube();
                }
            }

            if (ImGui::MenuItem("Create Sphere"))
            {
                if (m_OnCreateSphere)
                {
                    m_OnCreateSphere();
                }
            }

            if (ImGui::MenuItem("Create Quad"))
            {
                if (m_OnCreateQuad)
                {
                    m_OnCreateQuad();
                }
            }

            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void SceneHierarchyPanel::SetContextMenuActions(const std::function<void()>& onCreateEmpty, const std::function<void()>& onCreateCube,
        const std::function<void()>& onCreateSphere, const std::function<void()>& onCreateQuad)
    {
        m_OnCreateEmpty = onCreateEmpty;
        m_OnCreateCube = onCreateCube;
        m_OnCreateSphere = onCreateSphere;
        m_OnCreateQuad = onCreateQuad;
    }

    Trident::ECS::Entity SceneHierarchyPanel::GetSelectedEntity() const
    {
        return m_SelectedEntity;
    }

    void SceneHierarchyPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }
}