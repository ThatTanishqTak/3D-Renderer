#include "InspectorPanel.h"

#include <imgui.h>
#include <string>
#include <cstring>
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/CameraComponent.h"

#include <imgui.h>
#include <string>

namespace EditorPanels
{
    void InspectorPanel::Update()
    {
        // Rebuild the display label when the selection changes so Render can focus on UI presentation.
        if (m_Registry == nullptr || m_SelectedEntity == 0)
        {
            m_SelectedLabel = "None";
            return;
        }

        // Default label uses the entity identifier so the inspector is still useful without a tag.
        std::string l_Label = "Entity " + std::to_string(m_SelectedEntity);

        // Prefer the tag component when available for a more descriptive name.
        if (m_Registry->HasComponent<Trident::TagComponent>(m_SelectedEntity))
        {
            const auto& l_Tag = m_Registry->GetComponent<Trident::TagComponent>(m_SelectedEntity);
            if (!l_Tag.m_Tag.empty())
            {
                l_Label = l_Tag.m_Tag;
            }
        }

        m_SelectedLabel = l_Label;
    }

    void InspectorPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Inspector");
        (void)l_WindowVisible;
        // Submit inspector contents unconditionally so dockspace tests retain the node regardless of collapse.

        ImGui::TextWrapped("Selection: %s", m_SelectedLabel.c_str());
        ImGui::Separator();

        if (m_Registry == nullptr || m_SelectedEntity == 0)
        {
            ImGui::TextWrapped("No entity selected.");
            ImGui::End();
            return;
        }

        bool l_HasAnyComponent = false;

        // Tag component
        if (m_Registry->HasComponent<Trident::TagComponent>(m_SelectedEntity))
        {
            l_HasAnyComponent = true;

            auto& l_Tag = m_Registry->GetComponent<Trident::TagComponent>(m_SelectedEntity);

            if (ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen))
            {
                char l_Buffer[256];
                std::memset(l_Buffer, 0, sizeof(l_Buffer));
                std::strncpy(l_Buffer, l_Tag.m_Tag.c_str(), sizeof(l_Buffer) - 1);

                if (ImGui::InputText("Label", l_Buffer, sizeof(l_Buffer)))
                {
                    l_Tag.m_Tag = l_Buffer;

                    // Keep the cached label aligned with the tag changes.
                    if (!l_Tag.m_Tag.empty())
                    {
                        m_SelectedLabel = l_Tag.m_Tag;
                    }
                    else
                    {
                        m_SelectedLabel = "Entity " + std::to_string(m_SelectedEntity);
                    }
                }
            }
        }

        // Transform component
        if (m_Registry->HasComponent<Trident::Transform>(m_SelectedEntity))
        {
            l_HasAnyComponent = true;

            auto& l_Transform = m_Registry->GetComponent<Trident::Transform>(m_SelectedEntity);
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::DragFloat3("Position", &l_Transform.Position.x, 0.1f);
                ImGui::DragFloat3("Rotation", &l_Transform.Rotation.x, 0.5f);
                ImGui::DragFloat3("Scale", &l_Transform.Scale.x, 0.1f);
            }
        }

        // Mesh component
        if (m_Registry->HasComponent<Trident::MeshComponent>(m_SelectedEntity))
        {
            l_HasAnyComponent = true;

            auto& l_Mesh = m_Registry->GetComponent<Trident::MeshComponent>(m_SelectedEntity);
            if (ImGui::CollapsingHeader("Mesh"))
            {
                ImGui::Checkbox("Visible", &l_Mesh.m_Visible);

                const char* l_PrimitiveLabel = "None";
                switch (l_Mesh.m_Primitive)
                {
                case Trident::MeshComponent::PrimitiveType::Cube:   l_PrimitiveLabel = "Cube";   break;
                case Trident::MeshComponent::PrimitiveType::Sphere: l_PrimitiveLabel = "Sphere"; break;
                case Trident::MeshComponent::PrimitiveType::Quad:   l_PrimitiveLabel = "Quad";   break;
                default: break;
                }

                ImGui::TextWrapped("Primitive: %s", l_PrimitiveLabel);

                if (!l_Mesh.m_SourceAssetPath.empty())
                {
                    ImGui::TextWrapped("Source: %s", l_Mesh.m_SourceAssetPath.c_str());
                    ImGui::TextWrapped("Mesh Index: %zu", l_Mesh.m_SourceMeshIndex);
                }
            }
        }

        // Camera component
        if (m_Registry->HasComponent<Trident::CameraComponent>(m_SelectedEntity))
        {
            l_HasAnyComponent = true;

            auto& l_Camera = m_Registry->GetComponent<Trident::CameraComponent>(m_SelectedEntity);
            if (ImGui::CollapsingHeader("Camera"))
            {
                int l_ProjectionIndex =
                    (l_Camera.m_ProjectionType == Trident::Camera::ProjectionType::Perspective) ? 0 : 1;
                const char* l_ProjectionItems[] = { "Perspective", "Orthographic" };
                if (ImGui::Combo("Projection", &l_ProjectionIndex, l_ProjectionItems, 2))
                {
                    l_Camera.m_ProjectionType =
                        (l_ProjectionIndex == 0)
                        ? Trident::Camera::ProjectionType::Perspective
                        : Trident::Camera::ProjectionType::Orthographic;
                }

                if (l_Camera.m_ProjectionType == Trident::Camera::ProjectionType::Perspective)
                {
                    ImGui::DragFloat("Field of View", &l_Camera.m_FieldOfView, 0.1f, 1.0f, 179.0f);
                }
                else
                {
                    ImGui::DragFloat("Orthographic Size", &l_Camera.m_OrthographicSize, 0.1f, 0.1f, 1000.0f);
                }

                ImGui::DragFloat("Near Clip", &l_Camera.m_NearClip, 0.01f, 0.001f, l_Camera.m_FarClip - 0.01f);
                ImGui::DragFloat("Far Clip", &l_Camera.m_FarClip, 1.0f, l_Camera.m_NearClip + 0.01f, 10000.0f);

                ImGui::Checkbox("Primary", &l_Camera.m_Primary);
                ImGui::Checkbox("Fixed Aspect Ratio", &l_Camera.m_FixedAspectRatio);
                if (l_Camera.m_FixedAspectRatio)
                {
                    ImGui::DragFloat("Aspect Ratio", &l_Camera.m_AspectRatio, 0.01f, 0.1f, 10.0f);
                }
            }
        }

        if (!l_HasAnyComponent)
        {
            ImGui::TextWrapped("Selected entity has no recognised components.");
        }

        ImGui::End();
    }

    void InspectorPanel::SetSelectionLabel(const std::string& label)
    {
        m_SelectedLabel = label;
    }

    void InspectorPanel::SetGizmoState(Trident::GizmoState* gizmoState)
    {
        m_GizmoState = gizmoState;
    }

    void InspectorPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void InspectorPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }
}