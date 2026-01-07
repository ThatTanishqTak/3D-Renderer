#include "InspectorPanel.h"

#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/TextureComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/ScriptComponent.h"

#include <imgui.h>
#include <string>
#include <cstring>

namespace EditorPanels
{
    void InspectorPanel::Update()
    {
        // Rebuild the display label when the selection changes so Render can focus on UI presentation.
        // Skip label construction when no valid selection is present so stale IDs are not shown in the inspector.
        if (m_Registry == nullptr || m_SelectedEntity == s_InvalidEntity)
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

        // Abort rendering when the selection sentinel is active so UI widgets do not operate on invalid entities.
        if (m_Registry == nullptr || m_SelectedEntity == s_InvalidEntity)
        {
            ImGui::TextWrapped("No entity selected.");
            ImGui::End();
            return;
        }

        // Center the add-component button by offsetting the cursor with half of the remaining content width.
        const float l_ContentWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 l_ButtonLabelSize = ImGui::CalcTextSize("Add Component");
        const ImVec2 l_FramePadding = ImGui::GetStyle().FramePadding;
        const float l_ButtonWidth = l_ButtonLabelSize.x + (l_FramePadding.x * 2.0f);
        const float l_ButtonOffset = (l_ContentWidth - l_ButtonWidth) * 0.5f;
        if (l_ButtonOffset > 0.0f)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + l_ButtonOffset);
        }

        if (ImGui::Button("Add Component"))
        {
            // Use a stable popup name so ImGui can manage the menu state across frames.
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            // Disable menu items when the entity already owns the component to prevent duplicates.
            const bool l_HasCamera = m_Registry->HasComponent<Trident::CameraComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasCamera);
            if (ImGui::MenuItem("Camera Component"))
            {
                Trident::CameraComponent& l_Camera = m_Registry->AddComponent<Trident::CameraComponent>(m_SelectedEntity);
                l_Camera.m_Primary = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            const bool l_HasMesh = m_Registry->HasComponent<Trident::MeshComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasMesh);
            if (ImGui::MenuItem("Mesh Component"))
            {
                Trident::MeshComponent& l_Mesh = m_Registry->AddComponent<Trident::MeshComponent>(m_SelectedEntity);
                l_Mesh.m_Visible = true;
                l_Mesh.m_Primitive = Trident::MeshComponent::PrimitiveType::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            const bool l_HasLight = m_Registry->HasComponent<Trident::LightComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasLight);
            if (ImGui::MenuItem("Light Component"))
            {
                Trident::LightComponent& l_Light = m_Registry->AddComponent<Trident::LightComponent>(m_SelectedEntity);
                l_Light.m_Enabled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            const bool l_HasSprite = m_Registry->HasComponent<Trident::SpriteComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasSprite);
            if (ImGui::MenuItem("Sprite Component"))
            {
                Trident::SpriteComponent& l_Sprite = m_Registry->AddComponent<Trident::SpriteComponent>(m_SelectedEntity);
                l_Sprite.m_Visible = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            const bool l_HasTexture = m_Registry->HasComponent<Trident::TextureComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasTexture);
            if (ImGui::MenuItem("Texture Component"))
            {
                Trident::TextureComponent& l_Texture = m_Registry->AddComponent<Trident::TextureComponent>(m_SelectedEntity);
                l_Texture.m_IsDirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            const bool l_HasAnimation = m_Registry->HasComponent<Trident::AnimationComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasAnimation);
            if (ImGui::MenuItem("Animation Component"))
            {
                Trident::AnimationComponent& l_Animation = m_Registry->AddComponent<Trident::AnimationComponent>(m_SelectedEntity);
                l_Animation.m_IsPlaying = true;
                l_Animation.m_IsLooping = true;
                l_Animation.InvalidateCachedAssets();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            const bool l_HasScript = m_Registry->HasComponent<Trident::ScriptComponent>(m_SelectedEntity);
            ImGui::BeginDisabled(l_HasScript);
            if (ImGui::MenuItem("Script Component"))
            {
                Trident::ScriptComponent& l_Script = m_Registry->AddComponent<Trident::ScriptComponent>(m_SelectedEntity);
                l_Script.m_AutoStart = true;
                l_Script.m_IsRunning = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::EndPopup();
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

                // Present gizmo mode toggles so only one operation is active at a time.
                if (m_GizmoState != nullptr)
                {
                    int l_ModeIndex = 0; // Default to translate when no other mode is active to avoid null operations.
                    if (m_GizmoState->m_RotateEnabled)
                    {
                        l_ModeIndex = 1;
                    }
                    else if (m_GizmoState->m_ScaleEnabled)
                    {
                        l_ModeIndex = 2;
                    }

                    // Use radio buttons to force mutual exclusivity between translate/rotate/scale modes.
                    if (ImGui::RadioButton("Translate", l_ModeIndex == 0))
                    {
                        l_ModeIndex = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Rotate", l_ModeIndex == 1))
                    {
                        l_ModeIndex = 1;
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Scale", l_ModeIndex == 2))
                    {
                        l_ModeIndex = 2;
                    }

                    // Apply the selected mode back into the shared gizmo state so the viewport uses a single operation.
                    m_GizmoState->m_TranslateEnabled = l_ModeIndex == 0;
                    m_GizmoState->m_RotateEnabled = l_ModeIndex == 1;
                    m_GizmoState->m_ScaleEnabled = l_ModeIndex == 2;
                }
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