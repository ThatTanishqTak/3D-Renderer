#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

#include <functional>
#include <vector>
#include <string>

// The GameViewportPanel presents the runtime camera output inside the editor so designers can
// preview gameplay while retaining access to tooling. The implementation mirrors the editor
// ViewportPanel but trims responsibilities to focus on runtime observation.
class GameViewportPanel
{
public:
    GameViewportPanel();
    ~GameViewportPanel();

    // Called once per frame so the panel can respond to editor events prior to rendering widgets.
    void Update();
    // Draws the runtime viewport window and displays the renderer output.
    void Render();

    // Allows tools to subscribe to drag-and-drop payloads released over the game viewport.
    void SetAssetDropHandler(std::function<void(const std::vector<std::string>&)> assetDropHandler);
    // Mirrors the editor viewport context menu hook so future gameplay overlays can inject actions.
    void SetContextMenuHandler(std::function<void(const ImVec2&, const ImVec2&)> contextMenuHandler);
    // Exposes whether the panel's ImGui window is currently hovered.
    bool IsHovered() const { return m_IsHovered; }
    // Reports whether the window currently owns keyboard focus.
    bool IsFocused() const { return m_IsFocused; }

private:
    // Identifier supplied to the renderer so it can isolate runtime viewport resources.
    uint32_t m_ViewportID = 2U;
    // Cached ImGui size used to detect when the window is resized.
    glm::vec2 m_CachedViewportSize{ 0.0f };
    // Screen-space bounds of the rendered image for context and drag-drop handlers.
    ImVec2 m_ViewportBoundsMin{ 0.0f, 0.0f };
    ImVec2 m_ViewportBoundsMax{ 0.0f, 0.0f };
    // Tracks hover/focus state to coordinate runtime hotkeys without conflicting with editor tools.
    bool m_IsHovered = false;
    bool m_IsFocused = false;
    // Stores whether the window is currently open so the runtime camera can be suspended when hidden.
    bool m_IsWindowOpen = true;
    // Callback invoked when payloads are dropped on the viewport image.
    std::function<void(const std::vector<std::string>&)> m_OnAssetDrop{};
    // Callback surfaced immediately after drawing so overlays can register context menus.
    std::function<void(const ImVec2&, const ImVec2&)> m_OnViewportContextMenu{};
};