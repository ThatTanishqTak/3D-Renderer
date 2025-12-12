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
        // ID 1 is typically reserved for the Editor Scene Viewport
        m_ViewportInfo.ViewportID = 1U;
    }

    void SceneViewportPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Scene Viewport");
        (void)l_WindowVisible;

        // Setup Viewport Size & Render Target
        const ImVec2 l_Available = ImGui::GetContentRegionAvail();
        m_ViewportInfo.Size = { l_Available.x, l_Available.y };

        // Notify renderer of the current viewport size for resizing offscreen buffers
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        // Render the texture from the GPU
        SubmitViewportTexture(l_Available);

        // Handle Asset Drag & Drop from Content Browser while the viewport item is active
        // so the drop target matches the visible viewport bounds.
        if (m_OnAssetsDropped && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const std::string l_Path(reinterpret_cast<const char*>(l_Payload->Data), l_Payload->DataSize);
                m_OnAssetsDropped({ l_Path });
            }

            ImGui::EndDragDropTarget();
        }

        // Check if Gizmos are enabled, a valid entity is selected, and it has a Transform component
        if (m_GizmoState != nullptr && m_GizmoState->m_ShowGizmos && m_Registry != nullptr && m_SelectedEntity != s_InvalidEntity
            && m_Registry->HasComponent<Trident::Transform>(m_SelectedEntity))
        {
            // IMPORTANT: Reassert the viewport. This ensures RenderCommand internal state (like the active camera)
            // is set to the Editor Camera before we ask for matrices.
            Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

            // Get Camera Matrices
            glm::mat4 l_ViewMatrix = Trident::RenderCommand::GetEditorCameraViewMatrix();
            glm::mat4 l_ProjectionMatrix = Trident::RenderCommand::GetEditorCameraProjectionMatrix();

            l_ProjectionMatrix[1][1] *= -1.0f;

            // Determine Gizmo Operation (Translate, Rotate, or Scale)
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
                // Setup ImGuizmo Drawing
                ImGuizmo::BeginFrame();
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();

                // Set the rect to match the viewport bounds calculated in SubmitViewportTexture/Update
                ImGuizmo::SetRect(m_BoundsMin.x, m_BoundsMin.y, m_BoundsMax.x - m_BoundsMin.x, m_BoundsMax.y - m_BoundsMin.y);

                // Get Current Transform
                glm::mat4 l_ModelMatrix = Trident::RenderCommand::GetWorldTransform(m_SelectedEntity);

                // Draw and Handle Manipulation
                // Snap is currently not implemented but can be passed as the last argument if needed
                if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), l_Operation, ImGuizmo::LOCAL, glm::value_ptr(l_ModelMatrix)))
                {
                    // If manipulated, update the entity
                    glm::vec3 l_Translation{};
                    glm::vec3 l_Rotation{};
                    glm::vec3 l_Scale{};

                    // Decompose the new matrix back into TRS components
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(l_ModelMatrix), glm::value_ptr(l_Translation), glm::value_ptr(l_Rotation), glm::value_ptr(l_Scale));

                    // Send updated transform back to the engine
                    Trident::RenderCommand::SetWorldTransform(m_SelectedEntity, l_ModelMatrix);
                }
            }
        }

        // Update Hover/Focus state for input blocking
        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        ImGui::End();
    }

    void SceneViewportPanel::Update()
    {

    }

    // Getters and Setters implementation
    glm::vec2 SceneViewportPanel::GetViewportSize() const { return m_ViewportInfo.Size; }
    bool SceneViewportPanel::IsHovered() const { return m_IsHovered; }
    bool SceneViewportPanel::IsFocused() const { return m_IsFocused; }

    bool SceneViewportPanel::ContainsPoint(const ImVec2& screenPoint) const
    {
        return screenPoint.x >= m_BoundsMin.x && screenPoint.x <= m_BoundsMax.x && screenPoint.y >= m_BoundsMin.y && screenPoint.y <= m_BoundsMax.y;
    }

    Trident::ECS::Entity SceneViewportPanel::GetSelectedEntity() const { return m_SelectedEntity; }
    void SceneViewportPanel::SetGizmoState(Trident::GizmoState* gizmoState) { m_GizmoState = gizmoState; }

    void SceneViewportPanel::SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped)
    {
        m_OnAssetsDropped = onAssetsDropped;
    }

    void SceneViewportPanel::SetSelectedEntity(Trident::ECS::Entity entity) { m_SelectedEntity = entity; }
    void SceneViewportPanel::SetRegistry(Trident::ECS::Registry* registry) { m_Registry = registry; }

    void SceneViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {
        const VkDescriptorSet l_DescriptorSet = Trident::RenderCommand::GetViewportTexture(m_ViewportInfo.ViewportID);
        // Cast to ImTextureID (void*) for ImGui
        const ImTextureID l_TextureId = reinterpret_cast<ImTextureID>(l_DescriptorSet);

        if (l_TextureId != ImTextureID{ 0 } && viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            ImGui::Image(l_TextureId, viewportSize, ImVec2(0, 0), ImVec2(1, 1));

            // Cache the screen-space bounds of the image for ImGuizmo hit testing
            m_BoundsMin = ImGui::GetItemRectMin();
            m_BoundsMax = ImGui::GetItemRectMax();
        }
        else
        {
            ImGui::TextWrapped("Viewport unavailable");
            m_BoundsMin = ImGui::GetItemRectMin();
            m_BoundsMax = ImGui::GetItemRectMax();
        }
    }
}