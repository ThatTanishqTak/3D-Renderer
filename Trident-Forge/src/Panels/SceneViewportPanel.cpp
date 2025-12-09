#include "SceneViewportPanel.h"

#include "Renderer/RenderCommand.h"
#include "ECS/Components/TransformComponent.h"

#include <ImGuizmo.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace EditorPanels
{
    SceneViewportPanel::SceneViewportPanel()
    {
        m_ViewportInfo.ViewportID = 1U;
    }

    void SceneViewportPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Scene Viewport");
        (void)l_WindowVisible;
        // Always render viewport internals so dockspace and viewport tests see consistent submission even when collapsed

        const ImVec2 l_Available = ImGui::GetContentRegionAvail();
        m_ViewportInfo.Size = { l_Available.x, l_Available.y };
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        SubmitViewportTexture(l_Available);

        // Cache the screen-space bounds so external drop handlers can test OS-level cursor positions reliably
        const ImVec2 l_ContentMin = ImGui::GetWindowContentRegionMin();
        const ImVec2 l_ContentMax = ImGui::GetWindowContentRegionMax();
        const ImVec2 l_WindowPos = ImGui::GetWindowPos();

        m_BoundsMin = { l_WindowPos.x + l_ContentMin.x, l_WindowPos.y + l_ContentMin.y };
        m_BoundsMax = { l_WindowPos.x + l_ContentMax.x, l_WindowPos.y + l_ContentMax.y };

        // Draw an ImGuizmo-style overlay when the tool is enabled and the selected entity exposes a transform component.
        if (m_GizmoState != nullptr && m_GizmoState->m_ShowGizmos && m_Registry != nullptr && m_SelectedEntity != s_InvalidEntity
            && m_Registry->HasComponent<Trident::Transform>(m_SelectedEntity))
        {
            // Build the view and projection matrices from the renderer so the gizmo aligns with the scene camera.
            const glm::mat4 l_ViewMatrix = Trident::RenderCommand::GetViewportViewMatrix(m_ViewportInfo.ViewportID);
            const glm::mat4 l_ProjectionMatrix = Trident::RenderCommand::GetViewportProjectionMatrix(m_ViewportInfo.ViewportID);

            // Choose a single gizmo operation so ImGuizmo renders one mode at a time.
            ImGuizmo::OPERATION l_Operation = ImGuizmo::TRANSLATE;
            if (m_GizmoState->m_RotateEnabled)
            {
                l_Operation = ImGuizmo::ROTATE;
            }
            else if (m_GizmoState->m_ScaleEnabled)
            {
                l_Operation = ImGuizmo::SCALE;
            }

            if (m_GizmoState->m_TranslateEnabled || m_GizmoState->m_RotateEnabled || m_GizmoState->m_ScaleEnabled)
            {
                // Initialize the draw list and render area so ImGuizmo overlays the viewport texture correctly.
                ImGuizmo::BeginFrame();
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(m_BoundsMin.x, m_BoundsMin.y, m_BoundsMax.x - m_BoundsMin.x, m_BoundsMax.y - m_BoundsMin.y);

                // Request the resolved world transform so gizmo edits preserve scene graph relationships.
                glm::mat4 l_ModelMatrix = Trident::RenderCommand::GetWorldTransform(m_SelectedEntity);

                // Apply ImGuizmo manipulation and sync edits back to the registry and renderer when authors adjust the gizmo.
                if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), l_Operation, ImGuizmo::LOCAL,
                    glm::value_ptr(l_ModelMatrix)))
                {
                    glm::vec3 l_Translation{};
                    glm::vec3 l_Rotation{};
                    glm::vec3 l_Scale{};
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(l_ModelMatrix), glm::value_ptr(l_Translation), glm::value_ptr(l_Rotation), glm::value_ptr(l_Scale));

                    // Push the decomposed world values through the scene graph so children remain aligned.
                    Trident::RenderCommand::SetWorldTransform(m_SelectedEntity, l_ModelMatrix);
                }
            }
        }

        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        ImGui::End();
    }

    void SceneViewportPanel::Update()
    {
        // Handle ImGui drag-and-drop payloads that originate from inside the editor
        if (m_OnAssetsDropped && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const std::string l_Path(reinterpret_cast<const char*>(l_Payload->Data), l_Payload->DataSize);
                m_OnAssetsDropped({ l_Path });
            }

            ImGui::EndDragDropTarget();
        }
    }

    glm::vec2 SceneViewportPanel::GetViewportSize() const
    {
        return m_ViewportInfo.Size;
    }

    bool SceneViewportPanel::IsHovered() const
    {
        return m_IsHovered;
    }

    bool SceneViewportPanel::IsFocused() const
    {
        return m_IsFocused;
    }

    bool SceneViewportPanel::ContainsPoint(const ImVec2& screenPoint) const
    {
        return screenPoint.x >= m_BoundsMin.x && screenPoint.x <= m_BoundsMax.x && screenPoint.y >= m_BoundsMin.y && screenPoint.y <= m_BoundsMax.y;
    }

    Trident::ECS::Entity SceneViewportPanel::GetSelectedEntity() const
    {
        return m_SelectedEntity;
    }

    void SceneViewportPanel::SetGizmoState(Trident::GizmoState* gizmoState)
    {
        m_GizmoState = gizmoState;
    }

    void SceneViewportPanel::SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped)
    {
        m_OnAssetsDropped = onAssetsDropped;
    }

    void SceneViewportPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }

    void SceneViewportPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void SceneViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {
        const VkDescriptorSet l_DescriptorSet = Trident::RenderCommand::GetViewportTexture(m_ViewportInfo.ViewportID);
        const ImTextureID l_TextureId = reinterpret_cast<ImTextureID>(l_DescriptorSet);

        if (l_TextureId != ImTextureID{ 0 } && viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            ImGui::Image(l_TextureId, viewportSize, ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            ImGui::TextWrapped("Viewport unavailable");
        }
    }
}