#include "GameViewportPanel.h"

#include "Renderer/RenderCommand.h"

namespace EditorPanels
{
    GameViewportPanel::GameViewportPanel()
    {
        m_ViewportInfo.ViewportID = 2U;
    }

    void GameViewportPanel::Render()
    {
        if (!ImGui::Begin("Game Viewport"))
        {
            ImGui::End();
            return;
        }

        const ImVec2 l_Available = ImGui::GetContentRegionAvail();
        m_ViewportInfo.Size = { l_Available.x, l_Available.y };
        RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        SubmitViewportTexture(l_Available);

        ImGui::End();
    }

    void GameViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {
        const ImTextureID l_TextureId = RenderCommand::GetViewportTexture(m_ViewportInfo.ViewportID);
        if (l_TextureId != nullptr && viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            ImGui::Image(l_TextureId, viewportSize, ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            ImGui::TextUnformatted("Runtime viewport unavailable");
        }
    }
}