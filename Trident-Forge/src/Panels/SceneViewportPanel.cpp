#include "SceneViewportPanel.h"

#include "Renderer/RenderCommand.h"

namespace EditorPanels
{
    SceneViewportPanel::SceneViewportPanel()
    {
        m_ViewportInfo.ViewportID = 1U;
    }

    void SceneViewportPanel::Render()
    {
        if (!ImGui::Begin("Scene Viewport"))
        {
            ImGui::End();
            return;
        }

        const ImVec2 l_Available = ImGui::GetContentRegionAvail();
        m_ViewportInfo.Size = { l_Available.x, l_Available.y };
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        SubmitViewportTexture(l_Available);

        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

        ImGui::End();
    }

    glm::vec2 SceneViewportPanel::GetViewportSize() const
    {
        return m_ViewportInfo.Size;
    }

    bool SceneViewportPanel::IsHovered() const
    {
        return m_IsHovered;
    }

    void SceneViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {
        const ImTextureID l_TextureId = Trident::RenderCommand::GetViewportTexture((ImTextureID)m_ViewportInfo.ViewportID);

        // Swapchain-aware: leave sizing to ImGui while ensuring the Vulkan image view stays bound for the frame's
        // command buffer. This keeps us aligned with the ImGuiLayer render pass submission order handled by Renderer.
        if (l_TextureId != nullptr && viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            ImGui::Image(l_TextureId, viewportSize, ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            ImGui::TextUnformatted("Viewport unavailable");
        }
    }
}