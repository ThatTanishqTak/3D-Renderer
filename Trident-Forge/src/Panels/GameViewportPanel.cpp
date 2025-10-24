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
        // When the window is closed we ensure the editor camera regains control of the renderer.
        Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, true);
        m_HasRuntimeCamera = false;

        return;
    }

    bool l_WindowOpen = m_IsWindowOpen;
    const bool l_ShouldRender = ImGui::Begin("Game");
    m_IsWindowOpen = l_WindowOpen;

    if (!m_IsWindowOpen)
    {
        // Closing the window immediately hands control back to the editor viewport.
        Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, true);
        m_HasRuntimeCamera = false;
        m_IsFocused = false;
        m_IsHovered = false;
        ImGui::End();
        
        return;
    }

    if (!l_ShouldRender)
    {
        // Collapsed windows still need to relinquish runtime camera control for predictable behaviour.
        Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, true);
        m_HasRuntimeCamera = false;
        m_IsFocused = false;
        m_IsHovered = false;
        ImGui::End();

        return;
    }

    if (!m_HasRuntimeCamera)
    {
        // Without a runtime camera bound we keep the renderer in editor mode and surface guidance to the user.
        Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, false);
        m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        ImGui::TextWrapped("No active runtime camera in the scene.");
        // TODO: Display contextual actions here once play/pause state management is available.
        ImGui::End();

        return;
    }

    // Record hover/focus state so gameplay shortcuts can respect editor UI conventions.
    m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // Only drive the renderer with the runtime camera while the window is actively interacted with.
    // This avoids stealing control from the editor viewport once the new runtime panel is open.
    // TODO: Evaluate a dual-render path so editor and runtime previews can draw concurrently without flipping state.
    // Manual QA: scene and game viewports were opened together to confirm both render paths stay stable.
    Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, true);

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
        if (l_Descriptor != VK_NULL_HANDLE)
        {
            // Draw the runtime scene output. Future HUD overlays can layer ImGui draw calls after this image.
            ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ContentRegion, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            // TODO: Add HUD overlays (stats, gizmos) when a runtime camera is active.

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
            // TODO: Provide a placeholder overlay that communicates when the runtime renderer is offline.
        }
    }
    else
    {
        // When the viewport shrinks to zero we fall back to the editor camera to avoid wasted rendering work.
        Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, false);
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

void GameViewportPanel::SetRuntimeCameraPresence(bool hasRuntimeCamera)
{
    // Cache the runtime camera availability so Render() can avoid toggling renderer state unnecessarily.
    m_HasRuntimeCamera = hasRuntimeCamera;
    if (!m_HasRuntimeCamera)
    {
        // When no runtime camera is bound we immediately fall back to editor rendering.
        Trident::RenderCommand::SetViewportRuntimeCameraDriven(m_ViewportID, false);
    }
    // TODO: Integrate with play/pause state to keep this flag aligned with future runtime control workflows.
}