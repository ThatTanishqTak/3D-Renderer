#include "SceneViewportPanel.h"

#include "Renderer/RenderCommand.h"

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

        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        // Cache the screen-space bounds so external drop handlers can test OS-level cursor positions reliably
        const ImVec2 l_ContentMin = ImGui::GetWindowContentRegionMin();
        const ImVec2 l_ContentMax = ImGui::GetWindowContentRegionMax();
        const ImVec2 l_WindowPos = ImGui::GetWindowPos();

        m_BoundsMin = { l_WindowPos.x + l_ContentMin.x, l_WindowPos.y + l_ContentMin.y };
        m_BoundsMax = { l_WindowPos.x + l_ContentMax.x, l_WindowPos.y + l_ContentMax.y };

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