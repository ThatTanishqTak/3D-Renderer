#include "SceneHierarchyPanel.h"

#include <imgui.h>
#include <string>

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
        if (ImGui::Begin("Scene Hierarchy"))
        {
            ImGui::TextUnformatted(m_StatusMessage.c_str());
            ImGui::TextUnformatted("Hierarchy population will be driven by the active registry.");

            if (m_Registry != nullptr)
            {
                for (Trident::ECS::Entity it_Entity : m_Registry->GetEntities())
                {
                    const bool l_IsSelected = it_Entity == m_SelectedEntity;
                    const std::string l_Label = "Entity " + std::to_string(it_Entity);
                    if (ImGui::Selectable(l_Label.c_str(), l_IsSelected))
                    {
                        m_SelectedEntity = it_Entity;
                    }
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
        }

        ImGui::End();
    }

    void SceneHierarchyPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void SceneHierarchyPanel::SetContextMenuActions(const std::function<void()>& onCreateEmpty,
        const std::function<void()>& onCreateCube,
        const std::function<void()>& onCreateSphere,
        const std::function<void()>& onCreateQuad)
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