#include "GameViewportPanel.h"

#include "Renderer/RenderCommand.h"

#include <string>
#include <sstream>
#include <iomanip>

namespace EditorPanels
{
    GameViewportPanel::GameViewportPanel()
    {
        m_ViewportInfo.ViewportID = 2U;
    }

    void GameViewportPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Game Viewport");
        (void)l_WindowVisible;
        // Keep submission unconditional so dockspace stress tests retain the runtime viewport node consistently.

        const ImVec2 l_Available = ImGui::GetContentRegionAvail();
        m_ViewportInfo.Size = { l_Available.x, l_Available.y };
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        SubmitViewportTexture(l_Available);
        RenderFrameRateOverlay();

        // Keep hover/focus state in sync with the render path so runtime shortcuts can respect ImGui focus rules.
        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        ImGui::End();
    }

    void GameViewportPanel::Update()
    {
        // Surface asset drops routed through ImGui (e.g., dragging levels from the content browser).
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

    void GameViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {
        const VkDescriptorSet l_DescriptorSet = Trident::RenderCommand::GetViewportTexture(m_ViewportInfo.ViewportID);
        const ImTextureID l_TextureId = reinterpret_cast<ImTextureID>(l_DescriptorSet);

        // Respect ImGui's Vulkan backend expectations by treating zero as the invalid sentinel regardless of the
        // underlying ImTextureID typedef. This keeps runtime output aligned with the renderer's descriptor ownership.
        if (l_TextureId != ImTextureID{ 0 } && viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            ImGui::Image(l_TextureId, viewportSize, ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            ImGui::TextUnformatted("Runtime viewport unavailable");
        }
    }

    bool GameViewportPanel::IsHovered() const
    {
        return m_IsHovered;
    }

    bool GameViewportPanel::IsFocused() const
    {
        return m_IsFocused;
    }

    void GameViewportPanel::SetGizmoState(Trident::GizmoState* gizmoState)
    {
        m_GizmoState = gizmoState;
    }

    void GameViewportPanel::SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped)
    {
        m_OnAssetsDropped = onAssetsDropped;
    }

    void GameViewportPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void GameViewportPanel::RenderFrameRateOverlay()
    {
        // Pull averaged frame timing data from the renderer so the runtime viewport can surface the current FPS.
        const Trident::Renderer::FrameTimingStats l_FrameTimingStats = Trident::RenderCommand::GetFrameTimingStats();

        // Present the overlay only when the renderer has reported a valid timing sample.
        if (l_FrameTimingStats.AverageFPS <= 0.0)
        {
            return;
        }

        std::ostringstream l_FpsLabel{};
        l_FpsLabel << std::fixed << std::setprecision(1) << "FPS: " << l_FrameTimingStats.AverageFPS;

        const glm::vec2 l_TextPosition{ 10.0f, 30.0f };
        const glm::vec4 l_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Route the overlay through the renderer so text batching aligns with existing viewport submissions.
        Trident::RenderCommand::SubmitText(m_ViewportInfo.ViewportID, l_TextPosition, l_TextColor, l_FpsLabel.str());
    }
}