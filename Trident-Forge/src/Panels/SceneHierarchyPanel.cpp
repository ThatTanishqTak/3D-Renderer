#include "SceneHierarchyPanel.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <limits>

#include "Application.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/LightComponent.h"

namespace UI
{
    namespace
    {
        constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    }

    SceneHierarchyPanel::SceneHierarchyPanel() : m_SelectedEntity(s_InvalidEntity)
    {
        // Default the panel to no selection until the user interacts with the hierarchy.
    }

    void SceneHierarchyPanel::SetSelectedEntity(Trident::ECS::Entity selectedEntity)
    {
        m_SelectedEntity = selectedEntity;
    }

    Trident::ECS::Entity SceneHierarchyPanel::GetSelectedEntity() const
    {
        return m_SelectedEntity;
    }

    void SceneHierarchyPanel::Render()
    {
        if (!ImGui::Begin("World Outliner"))
        {
            ImGui::End();

            return;
        }

        Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();
        DrawEntityList(l_Registry);
        DrawLightCreationButtons(l_Registry);

        ImGui::End();
    }

    void SceneHierarchyPanel::DrawEntityList(Trident::ECS::Registry& registry)
    {
        const std::vector<Trident::ECS::Entity>& l_Entities = registry.GetEntities();

        if (l_Entities.empty())
        {
            ImGui::TextUnformatted("No entities in the active scene.");
            m_SelectedEntity = s_InvalidEntity;

            return;
        }

        ImGui::Text("Entities (%zu)", l_Entities.size());
        if (ImGui::BeginListBox("##WorldOutlinerList"))
        {
            for (Trident::ECS::Entity it_Entity : l_Entities)
            {
                bool l_IsSelected = it_Entity == m_SelectedEntity;
                std::string l_Label = "Entity " + std::to_string(static_cast<unsigned int>(it_Entity));
                if (ImGui::Selectable(l_Label.c_str(), l_IsSelected))
                {
                    m_SelectedEntity = it_Entity;
                }

                if (l_IsSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndListBox();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Create Light Entity");
    }

    void SceneHierarchyPanel::DrawLightCreationButtons(Trident::ECS::Registry& registry)
    {
        if (ImGui::Button("Directional Light"))
        {
            Trident::ECS::Entity l_NewEntity = registry.CreateEntity();
            Trident::Transform& l_EntityTransform = registry.AddComponent<Trident::Transform>(l_NewEntity);
            l_EntityTransform.Position = { 0.0f, 5.0f, 0.0f };

            Trident::LightComponent& l_Light = registry.AddComponent<Trident::LightComponent>(l_NewEntity);
            l_Light.m_Type = Trident::LightComponent::Type::Directional;
            l_Light.m_Direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
            l_Light.m_Intensity = 5.0f;

            m_SelectedEntity = l_NewEntity;
        }

        if (ImGui::Button("Point Light"))
        {
            Trident::ECS::Entity l_NewEntity = registry.CreateEntity();
            Trident::Transform& l_EntityTransform = registry.AddComponent<Trident::Transform>(l_NewEntity);
            l_EntityTransform.Position = { 0.0f, 2.0f, 0.0f };

            Trident::LightComponent& l_Light = registry.AddComponent<Trident::LightComponent>(l_NewEntity);
            l_Light.m_Type = Trident::LightComponent::Type::Point;
            l_Light.m_Range = 10.0f;
            l_Light.m_Intensity = 25.0f;

            m_SelectedEntity = l_NewEntity;
        }

        ImGui::TextUnformatted("Future enhancements: add folders, filtering, and drag-and-drop reparenting.");
    }
}