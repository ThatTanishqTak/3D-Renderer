#include "GameViewportPanel.h"

#include <string>

namespace EditorPanels
{
    GameViewportPanel::GameViewportPanel()
    {

    }

    void GameViewportPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Game Viewport");
        (void)l_WindowVisible;
        // Keep submission unconditional so dockspace stress tests retain the runtime viewport node consistently.

        const ImVec2 l_Available = ImGui::GetContentRegionAvail();

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