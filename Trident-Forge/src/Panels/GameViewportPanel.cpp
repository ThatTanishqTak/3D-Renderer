#include "GameViewportPanel.h"

#include "Renderer/RenderCommand.h"

#include <string>

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
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        SubmitViewportTexture(l_Available);

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
}