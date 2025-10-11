#include "ViewportPanel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Application.h"
#include "Camera/Camera.h"
#include "Camera/CameraComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Registry.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"

namespace
{
    /**
     * @brief Build a transform matrix from a Trident transform component.
     */
    glm::mat4 ComposeTransform(const Trident::Transform& a_Transform)
    {
        glm::mat4 l_ModelMatrix{ 1.0f };
        l_ModelMatrix = glm::translate(l_ModelMatrix, a_Transform.Position);
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_ModelMatrix = glm::scale(l_ModelMatrix, a_Transform.Scale);

        return l_ModelMatrix;
    }

    /**
     * @brief Construct a projection matrix matching the supplied camera component.
     */
    glm::mat4 BuildCameraProjectionMatrix(const Trident::CameraComponent& a_CameraComponent, float a_ViewportAspect)
    {
        float l_Aspect = a_CameraComponent.OverrideAspectRatio ? a_CameraComponent.AspectRatio : a_ViewportAspect;
        l_Aspect = std::max(l_Aspect, 0.0001f);

        if (a_CameraComponent.UseCustomProjection)
        {
            return a_CameraComponent.CustomProjection;
        }

        if (a_CameraComponent.Projection == Trident::ProjectionType::Orthographic)
        {
            const float l_HalfHeight = a_CameraComponent.OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;
            glm::mat4 l_Projection = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, a_CameraComponent.NearClip, a_CameraComponent.FarClip);
            l_Projection[1][1] *= -1.0f;
            return l_Projection;
        }

        glm::mat4 l_Projection = glm::perspective(glm::radians(a_CameraComponent.FieldOfView), l_Aspect, a_CameraComponent.NearClip, a_CameraComponent.FarClip);
        l_Projection[1][1] *= -1.0f;
        return l_Projection;
    }
}

namespace UI
{
    ViewportPanel::ViewportPanel() : m_SelectedViewportCamera(s_InvalidEntity), m_SelectedEntity(s_InvalidEntity), m_SelectedCameraIndex(0)
    {

    }

    void ViewportPanel::SetSelectedEntity(Trident::ECS::Entity selectedEntity)
    {
        m_SelectedEntity = selectedEntity;
    }

    Trident::ECS::Entity ViewportPanel::GetSelectedEntity() const
    {
        return m_SelectedEntity;
    }

    Trident::ECS::Entity ViewportPanel::GetSelectedCamera() const
    {
        return m_SelectedViewportCamera;
    }

    void ViewportPanel::Render()
    {
        // The primary viewport renders the scene output and provides high-level camera assignment hooks.
        if (!ImGui::Begin("Viewport"))
        {
            ImGui::End();

            return;
        }

        const ImVec2 l_PanelOrigin = ImGui::GetCursorScreenPos();
        const ImVec2 l_PanelAvailable = ImGui::GetContentRegionAvail();

        Trident::ViewportInfo l_Viewport{};
        if (const ImGuiViewport* l_WindowViewport = ImGui::GetWindowViewport(); l_WindowViewport != nullptr)
        {
            l_Viewport.ViewportID = static_cast<uint32_t>(l_WindowViewport->ID);
        }
        l_Viewport.Position = glm::vec2{ l_PanelOrigin.x, l_PanelOrigin.y };
        l_Viewport.Size = glm::vec2
        {
            std::max(l_PanelAvailable.x, 0.0f),
            std::max(l_PanelAvailable.y, 0.0f)
        };

        Trident::RenderCommand::SetViewport(l_Viewport);

        Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();

        struct ViewportCameraOption
        {
            Trident::ECS::Entity Entity = s_InvalidEntity;
            std::string Label{};
        };

        std::vector<ViewportCameraOption> l_CameraOptions{};
        l_CameraOptions.reserve(8);
        l_CameraOptions.push_back({ s_InvalidEntity, "Editor Camera (Free)" });

        const std::vector<Trident::ECS::Entity>& l_AllEntities = l_Registry.GetEntities();
        for (Trident::ECS::Entity it_Entity : l_AllEntities)
        {
            if (!l_Registry.HasComponent<Trident::CameraComponent>(it_Entity))
            {
                continue;
            }

            const Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(it_Entity);
            std::string l_Label = l_CameraComponent.Name;
            if (l_Label.empty())
            {
                l_Label = "Camera " + std::to_string(static_cast<unsigned int>(it_Entity));
            }

            l_CameraOptions.push_back({ it_Entity, std::move(l_Label) });
        }

        if (m_SelectedCameraIndex >= static_cast<int>(l_CameraOptions.size()))
        {
            m_SelectedCameraIndex = 0;
        }

        const auto a_ItemGetter = [](void* a_UserData, int a_Index, const char** a_OutText) -> bool
            {
                auto* l_Options = static_cast<std::vector<ViewportCameraOption>*>(a_UserData);
                if (a_Index < 0 || a_Index >= static_cast<int>(l_Options->size()))
                {
                    return false;
                }

                *a_OutText = (*l_Options)[a_Index].Label.c_str();
                return true;
            };

        const bool l_CameraChanged = ImGui::Combo("Viewport Camera", &m_SelectedCameraIndex, a_ItemGetter, static_cast<void*>(&l_CameraOptions), static_cast<int>(l_CameraOptions.size()));

        const Trident::ECS::Entity l_CurrentCameraEntity = l_CameraOptions[m_SelectedCameraIndex].Entity;
        if (l_CameraChanged || l_CurrentCameraEntity != m_SelectedViewportCamera)
        {
            m_SelectedViewportCamera = l_CurrentCameraEntity;
            Trident::RenderCommand::SetViewportCamera(m_SelectedViewportCamera);
        }

        // Future slot: add a cinematic toolbar or sequencer controls to trigger play-in-editor clips.

        glm::vec4 l_ClearColor = Trident::RenderCommand::GetClearColor();
        if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(l_ClearColor)))
        {
            Trident::RenderCommand::SetClearColor(l_ClearColor);
        }

        const ImVec2 l_ImageSize{ l_Viewport.Size.x, l_Viewport.Size.y };
        const VkDescriptorSet l_ViewportTexture = Trident::RenderCommand::GetViewportTexture();
        if (l_ViewportTexture != VK_NULL_HANDLE && l_ImageSize.x > 0.0f && l_ImageSize.y > 0.0f)
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(l_ViewportTexture), l_ImageSize);

            // Clearing the selection when the user clicks empty space keeps the editor behavior consistent with other DCC tools.
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                // Avoid clearing while a gizmo drag is active so transform edits are not interrupted mid-manipulation.
                const bool l_GizmoActive = ImGuizmo::IsUsing();
                const bool l_GizmoHovered = ImGuizmo::IsOver();
                if (!l_GizmoActive && !l_GizmoHovered)
                {
                    m_SelectedEntity = s_InvalidEntity;
                }
            }

            const ImVec2 l_ImageMin = ImGui::GetItemRectMin();
            const ImVec2 l_ImageMax = ImGui::GetItemRectMax();
            const ImVec2 l_ImageExtent{ l_ImageMax.x - l_ImageMin.x, l_ImageMax.y - l_ImageMin.y };

            glm::mat4 l_ViewMatrix{ 1.0f };
            glm::mat4 l_ProjectionMatrix{ 1.0f };

            if (m_SelectedViewportCamera != s_InvalidEntity && l_Registry.HasComponent<Trident::CameraComponent>(m_SelectedViewportCamera)
                && l_Registry.HasComponent<Trident::Transform>(m_SelectedViewportCamera))
            {
                const Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(m_SelectedViewportCamera);
                const Trident::Transform& l_CameraTransform = l_Registry.GetComponent<Trident::Transform>(m_SelectedViewportCamera);

                const glm::mat4 l_ModelMatrix = ComposeTransform(l_CameraTransform);
                l_ViewMatrix = glm::inverse(l_ModelMatrix);
                const float l_AspectRatio = l_ImageExtent.y > 0.0f ? l_ImageExtent.x / l_ImageExtent.y : 1.0f;
                l_ProjectionMatrix = BuildCameraProjectionMatrix(l_CameraComponent, l_AspectRatio);
            }
            else
            {
                Trident::Camera& l_EditorCamera = Trident::Application::GetRenderer().GetCamera();
                l_ViewMatrix = l_EditorCamera.GetViewMatrix();
                const float l_AspectRatio = l_ImageExtent.y > 0.0f ? l_ImageExtent.x / l_ImageExtent.y : 1.0f;
                l_ProjectionMatrix = glm::perspective(glm::radians(l_EditorCamera.GetFOV()), l_AspectRatio, l_EditorCamera.GetNearClip(), l_EditorCamera.GetFarClip());
                l_ProjectionMatrix[1][1] *= -1.0f;
            }

            struct ViewportOverlayPrimitive
            {
                enum class Type
                {
                    Crosshair,
                    Text
                };

                Type PrimitiveType = Type::Crosshair;
                ImVec2 Position0{};
                ImVec2 Position1{};
                ImU32 Color = IM_COL32(255, 255, 255, 255);
                float Thickness = 1.0f;
                std::string Label{};
            };

            std::vector<ViewportOverlayPrimitive> l_OverlayPrimitives{};
            l_OverlayPrimitives.reserve(4);

            if (m_SelectedEntity != s_InvalidEntity)
            {
                if (l_Registry.HasComponent<Trident::Transform>(m_SelectedEntity))
                {
                    const Trident::Transform& l_SelectedTransform = l_Registry.GetComponent<Trident::Transform>(m_SelectedEntity);

                    const glm::mat4 l_ModelMatrix = ComposeTransform(l_SelectedTransform);
                    const glm::vec4 l_WorldCenter = l_ModelMatrix * glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };

                    const glm::vec4 l_ClipSpace = l_ProjectionMatrix * l_ViewMatrix * l_WorldCenter;
                    if (l_ClipSpace.w > 0.0f)
                    {
                        const glm::vec3 l_Ndc = glm::vec3{ l_ClipSpace } / l_ClipSpace.w;

                        if (std::abs(l_Ndc.x) <= 1.0f && std::abs(l_Ndc.y) <= 1.0f && l_Ndc.z >= -1.0f && l_Ndc.z <= 1.0f)
                        {
                            const float l_ScreenX = l_ImageMin.x + ((l_Ndc.x * 0.5f) + 0.5f) * l_ImageExtent.x;
                            const float l_ScreenY = l_ImageMin.y + ((-l_Ndc.y * 0.5f) + 0.5f) * l_ImageExtent.y;
                            const ImVec2 l_ScreenPosition{ l_ScreenX, l_ScreenY };

                            ViewportOverlayPrimitive l_Crosshair{};
                            l_Crosshair.PrimitiveType = ViewportOverlayPrimitive::Type::Crosshair;
                            l_Crosshair.Position0 = l_ScreenPosition;
                            l_Crosshair.Position1 = ImVec2{ 8.0f, 8.0f };
                            l_Crosshair.Color = IM_COL32(255, 215, 0, 255);
                            l_Crosshair.Thickness = 1.5f;
                            l_OverlayPrimitives.emplace_back(l_Crosshair);

                            ViewportOverlayPrimitive l_Label{};
                            l_Label.PrimitiveType = ViewportOverlayPrimitive::Type::Text;
                            l_Label.Position0 = ImVec2{ l_ScreenPosition.x + 10.0f, l_ScreenPosition.y - ImGui::GetTextLineHeightWithSpacing() };
                            l_Label.Color = IM_COL32(255, 255, 255, 255);
                            l_Label.Label = "Entity " + std::to_string(static_cast<unsigned int>(m_SelectedEntity));
                            l_OverlayPrimitives.emplace_back(std::move(l_Label));
                        }
                    }
                }
            }

            ImDrawList* l_DrawList = ImGui::GetWindowDrawList();
            if (l_DrawList != nullptr)
            {
                for (const ViewportOverlayPrimitive& it_Primitive : l_OverlayPrimitives)
                {
                    switch (it_Primitive.PrimitiveType)
                    {
                    case ViewportOverlayPrimitive::Type::Crosshair:
                    {
                        const ImVec2 l_HorizontalStart{ it_Primitive.Position0.x - it_Primitive.Position1.x, it_Primitive.Position0.y };
                        const ImVec2 l_HorizontalEnd{ it_Primitive.Position0.x + it_Primitive.Position1.x, it_Primitive.Position0.y };
                        const ImVec2 l_VerticalStart{ it_Primitive.Position0.x, it_Primitive.Position0.y - it_Primitive.Position1.y };
                        const ImVec2 l_VerticalEnd{ it_Primitive.Position0.x, it_Primitive.Position0.y + it_Primitive.Position1.y };
                        l_DrawList->AddLine(l_HorizontalStart, l_HorizontalEnd, it_Primitive.Color, it_Primitive.Thickness);
                        l_DrawList->AddLine(l_VerticalStart, l_VerticalEnd, it_Primitive.Color, it_Primitive.Thickness);

                        break;
                    }
                    case ViewportOverlayPrimitive::Type::Text:
                        l_DrawList->AddText(it_Primitive.Position0, it_Primitive.Color, it_Primitive.Label.c_str());
                        break;
                    default:
                        break;
                    }
                }
            }

            // TODO: Expand overlay collection to include safe-frame guides, grid snapping hints, and gizmo layers for additional editor polish.
        }
        else
        {
            ImGui::TextUnformatted("Scene viewport not ready.");
        }

        ImGui::End();
    }
}