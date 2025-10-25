#include "GameViewportPanel.h"

#include "Renderer/RenderCommand.h"

#include <vulkan/vulkan.h>

#include <utility>

GameViewportPanel::GameViewportPanel()
{
    // Runtime viewport does not require additional bootstrapping yet. Hooks can be added here later.
}

GameViewportPanel::~GameViewportPanel()
{
    // Panel owns no resources today. Future extensions can release overlay state here if needed.
}

void GameViewportPanel::Update()
{
    // The runtime viewport currently has no per-frame preparation work.
    // Future gameplay tooling could read input here to drive debugging widgets.
}

void GameViewportPanel::Render()
{
    if (!m_IsWindowOpen)
    {
        // When the window is closed we simply skip drawing. Future runtime playback will reintroduce camera ownership rules.
        m_IsFocused = false;
        m_IsHovered = false;

        return;
    }

    bool l_WindowOpen = m_IsWindowOpen;
    const bool l_ShouldRender = ImGui::Begin("Game");
    m_IsWindowOpen = l_WindowOpen;

    if (!m_IsWindowOpen)
    {
        // Closing the window clears transient state. Rendering will resume next frame if the user reopens the tab.
        m_IsFocused = false;
        m_IsHovered = false;
        ImGui::End();
        
        return;
    }

    if (!l_ShouldRender)
    {
        // Collapsed windows do not need to draw an image but we still clear state to keep interactions deterministic.
        m_IsFocused = false;
        m_IsHovered = false;
        ImGui::End();

        return;
    }

    // Record hover/focus state so gameplay shortcuts can respect editor UI conventions.
    m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    const ImVec2 l_ContentRegion = ImGui::GetContentRegionAvail();
    const glm::vec2 l_NewViewportSize{ l_ContentRegion.x, l_ContentRegion.y };

    if (l_NewViewportSize.x > 0.0f && l_NewViewportSize.y > 0.0f)
    {
        const ImVec2 l_ViewportPos = ImGui::GetCursorScreenPos();
        m_ViewportBoundsMin = l_ViewportPos;
        m_ViewportBoundsMax = ImVec2(l_ViewportPos.x + l_ContentRegion.x, l_ViewportPos.y + l_ContentRegion.y);

        if (l_NewViewportSize != m_CachedViewportSize)
        {
            Trident::ViewportInfo l_Info{};
            l_Info.ViewportID = m_ViewportID;
            l_Info.Position = { l_ViewportPos.x, l_ViewportPos.y };
            l_Info.Size = l_NewViewportSize;
            Trident::RenderCommand::SetViewport(m_ViewportID, l_Info);

            m_CachedViewportSize = l_NewViewportSize;
        }

        const VkDescriptorSet l_Descriptor = Trident::RenderCommand::GetViewportTexture(m_ViewportID);
        const bool l_HasRuntimeCamera = Trident::RenderCommand::HasRuntimeCamera();
        if (l_Descriptor != VK_NULL_HANDLE && l_HasRuntimeCamera)
        {
            // Draw the runtime scene output. The renderer now routes gameplay through the dedicated runtime camera.
            ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ContentRegion, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            // TODO: Add HUD overlays (stats, gizmos) when a runtime camera is active and validated.

            if (m_OnViewportContextMenu)
            {
                // Surface the bounds immediately so external systems can append custom context menus.
                m_OnViewportContextMenu(m_ViewportBoundsMin, m_ViewportBoundsMax);
            }

            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM");
                if (l_Payload != nullptr && l_Payload->Data != nullptr && l_Payload->DataSize > 0)
                {
                    const char* l_PathData = static_cast<const char*>(l_Payload->Data);
                    std::string l_PathString{ l_PathData, l_PathData + (l_Payload->DataSize - 1) };

                    if (m_OnAssetDrop)
                    {
                        std::vector<std::string> l_DroppedPaths{};
                        l_DroppedPaths.emplace_back(std::move(l_PathString));
                        m_OnAssetDrop(l_DroppedPaths);
                    }
                    // TODO: Support additional payload types for gameplay debugging assets.
                }
                ImGui::EndDragDropTarget();
            }
        }
        else
        {
            // Draw an overlay so users understand why the viewport is empty. Future revisions can surface status icons here
            // (for example, indicating the editor camera will be shown as a fallback once runtime playback resumes).
            ImDrawList* l_DrawList = ImGui::GetWindowDrawList();
            const char* l_Message = l_HasRuntimeCamera ? "Waiting for Runtime Render Target" : "No Active Runtime Camera is Present";
            const ImVec2 l_TextSize = ImGui::CalcTextSize(l_Message);
            const ImVec2 l_ViewportCenter{ (m_ViewportBoundsMin.x + m_ViewportBoundsMax.x) * 0.5f, (m_ViewportBoundsMin.y + m_ViewportBoundsMax.y) * 0.5f };
            const ImVec2 l_TextPosition{ l_ViewportCenter.x - (l_TextSize.x * 0.5f), l_ViewportCenter.y - (l_TextSize.y * 0.5f) };
            const ImU32 l_TextColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            // Anchor the notice in the middle of the viewport so it remains readable regardless of panel size.
            l_DrawList->AddText(l_TextPosition, l_TextColor, l_Message);
            // TODO: Pipe diagnostic overlays (e.g., active camera entity or error reason) into this branch for rapid debugging.
        }
    }

    // Additional runtime metrics and overlays can be drawn here before closing the window.
    ImGui::End();
}

void GameViewportPanel::SetAssetDropHandler(std::function<void(const std::vector<std::string>&)> assetDropHandler)
{
    // Store the callback so gameplay tooling can capture drag-and-drop events.
    //m_OnAssetDrop = std::move(assetDropHandler);
}

void GameViewportPanel::SetContextMenuHandler(std::function<void(const ImVec2&, const ImVec2&)> contextMenuHandler)
{
    // Cache the handler so external systems can attach context menus to the runtime viewport image.
    //m_OnViewportContextMenu = std::move(contextMenuHandler);
}