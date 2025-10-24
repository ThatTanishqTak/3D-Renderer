#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

#include <functional>
#include <string>
#include <vector>
#include <limits>

#include "Renderer/RenderCommand.h"
#include "ECS/Entity.h"
#include "GizmoState.h"

namespace ImGuizmo
{
    enum OPERATION;
    enum MODE;
}

// The ViewportPanel encapsulates the editor-facing viewport widget, wiring up ImGui with
// Trident's renderer so the off-screen scene texture is displayed in the UI.
class ViewportPanel
{
public:
    // Constructor initialises default state so the panel is immediately interactive.
    ViewportPanel();
    // Called once per frame so the panel can prepare state prior to rendering widgets.
    void Update();
    // Draws the viewport ImGui window and presents the renderer output.
    void Render();
    // Provides a handle to the shared gizmo state so UI panels can coordinate behaviour.
    void SetGizmoState(GizmoState* gizmoState);
    // Allow the viewport to react when the hierarchy/inspector selection changes.
    void SetSelectedEntity(Trident::ECS::Entity selectedEntity);
    // Allows editor systems to react when assets are dropped onto the viewport image.
    void SetAssetDropHandler(std::function<void(const std::vector<std::string>&)> assetDropHandler);
    // Exposes whether the viewport is hovered so external systems can gate drag-and-drop behaviour.
    bool IsHovered() const { return m_IsHovered; }
    // Reports whether the viewport window currently owns keyboard focus for camera control decisions.
    bool IsFocused() const { return m_IsFocused; }
    // Provide read-only access to the viewport identifier so callers can forward it into renderer APIs.
    uint32_t GetViewportID() const { return m_ViewportID; }
    // Allows callers to check whether a screen-space point falls within the viewport image.
    bool ContainsPoint(const ImVec2& point) const;

private:
    // Frame the current selection or world origin when the user presses the focus key.
    void FrameSelection();

private:
    // Persistent identifier used when asking the renderer for a viewport.
    uint32_t m_ViewportID = 1U;
    // Cached dimensions so we can detect when the window is resized.
    glm::vec2 m_CachedViewportSize{ 0.0f };
    // Records the most recent screen-space bounds of the viewport image for drag-and-drop hit testing.
    ImVec2 m_ViewportBoundsMin{ 0.0f, 0.0f };
    ImVec2 m_ViewportBoundsMax{ 0.0f, 0.0f };
    // Tracks whether the window is focused so we can route input to camera controls.
    bool m_IsFocused = false;
    // Tracks whether the window is hovered, used in combination with focus to enable tools.
    bool m_IsHovered = false;
    // Stores whether camera controls should be active for the current frame.
    bool m_IsCameraControlEnabled = false;
    // Camera entity currently driving the viewport render target.
    Trident::ECS::Entity m_ActiveCameraEntity = 0;
    // Tracks the entity selected in the hierarchy/inspector for pivot updates.
    Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    // Cache the previous entity so pivot updates only trigger on changes.
    Trident::ECS::Entity m_PreviousSelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    // Shared gizmo configuration so this panel stays in sync with inspector controls.
    GizmoState* m_GizmoState = nullptr;
    // Callback invoked whenever a drag-and-drop payload is released over the viewport image.
    std::function<void(const std::vector<std::string>&)> m_OnAssetDrop{};
};