#include "InspectorPanel.h"

#include "Application/Startup.h"
#include "ECS/Registry.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

void InspectorPanel::SetSelectedEntity(Trident::ECS::Entity entity)
{
    m_SelectedEntity = entity;
    if (m_GizmoState != nullptr)
    {
        const bool l_HasSelection = m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max();
        m_GizmoState->SetSelectionActive(l_HasSelection);
    }
}

void InspectorPanel::SetGizmoState(GizmoState* gizmoState)
{
    // Hold onto the shared gizmo state so radio buttons can drive the viewport overlay.
    m_GizmoState = gizmoState;

    if (m_GizmoState != nullptr)
    {
        const bool l_HasSelection = m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max();
        m_GizmoState->SetSelectionActive(l_HasSelection);
    }
}

void InspectorPanel::Update()
{
    if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
    {
        // Nothing to validate when no entity is selected.
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();
    const bool l_SelectionStillExists = std::find(l_Entities.begin(), l_Entities.end(), m_SelectedEntity) != l_Entities.end();
    if (!l_SelectionStillExists)
    {
        // Clear the cached selection so the inspector avoids dereferencing stale components.
        m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
        if (m_GizmoState != nullptr)
        {
            m_GizmoState->SetSelectionActive(false);
        }
    }
}

void InspectorPanel::Render()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();

        return;
    }

    if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
    {
        ImGui::TextWrapped("Select an entity from the Scene Hierarchy to inspect its components.");
        if (m_GizmoState != nullptr)
        {
            // Ensure the viewport hides the gizmo if the selection was cleared while the window was collapsed.
            m_GizmoState->SetSelectionActive(false);
        }
        ImGui::End();

        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();

    DrawTagComponent(l_Registry);
    DrawTransformComponent(l_Registry);

    // Future improvement: reflect over registered component types to avoid manual draw helpers.

    ImGui::End();
}

void InspectorPanel::DrawTagComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::TagComponent>(m_SelectedEntity))
    {
        return;
    }

    if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Trident::TagComponent& l_TagComponent = registry.GetComponent<Trident::TagComponent>(m_SelectedEntity);

        std::array<char, 256> l_TagBuffer{};
        std::strncpy(l_TagBuffer.data(), l_TagComponent.m_Tag.c_str(), l_TagBuffer.size() - 1);

        if (ImGui::InputText("Label", l_TagBuffer.data(), l_TagBuffer.size()))
        {
            // Persist the edited label back onto the component. Rely on std::string to trim trailing nulls.
            l_TagComponent.m_Tag = l_TagBuffer.data();
        }
    }
}

void InspectorPanel::DrawTransformComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::Transform>(m_SelectedEntity))
    {
        return;
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Trident::Transform& l_Transform = registry.GetComponent<Trident::Transform>(m_SelectedEntity);

        // Provide ergonomic controls tuned for editor-like precision.
        ImGui::DragFloat3("Position", glm::value_ptr(l_Transform.Position), 0.1f, -10000.0f, 10000.0f, "%.2f");
        ImGui::DragFloat3("Rotation", glm::value_ptr(l_Transform.Rotation), 0.1f, -360.0f, 360.0f, "%.2f");
        ImGui::DragFloat3("Scale", glm::value_ptr(l_Transform.Scale), 0.01f, 0.0f, 1000.0f, "%.2f");

        if (m_GizmoState != nullptr)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Gizmo Controls");

            // Mirror the viewport radio buttons here so users can change the active operation from the inspector.
            if (ImGui::RadioButton("Translate", m_GizmoState->GetOperation() == ImGuizmo::TRANSLATE))
            {
                m_GizmoState->SetOperation(ImGuizmo::TRANSLATE);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", m_GizmoState->GetOperation() == ImGuizmo::ROTATE))
            {
                m_GizmoState->SetOperation(ImGuizmo::ROTATE);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", m_GizmoState->GetOperation() == ImGuizmo::SCALE))
            {
                m_GizmoState->SetOperation(ImGuizmo::SCALE);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("TODO: Snapping / Pivot");

            if (ImGui::RadioButton("Local", m_GizmoState->GetMode() == ImGuizmo::LOCAL))
            {
                m_GizmoState->SetMode(ImGuizmo::LOCAL);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("World", m_GizmoState->GetMode() == ImGuizmo::WORLD))
            {
                m_GizmoState->SetMode(ImGuizmo::WORLD);
            }

            // Future opportunity: expose snapping controls once the engine supports grid-aligned editing.
        }
    }
}