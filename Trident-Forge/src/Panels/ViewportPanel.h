#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

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
    // Allows external systems to provide the camera that should drive the viewport.
    void SetCameraEntity(Trident::ECS::Entity cameraEntity);
    // Provides a handle to the shared gizmo state so UI panels can coordinate behaviour.
    void SetGizmoState(GizmoState* gizmoState);

private:
    // Persistent identifier used when asking the renderer for a viewport.
    uint32_t m_ViewportID = 1U;
    // Cached dimensions so we can detect when the window is resized.
    glm::vec2 m_CachedViewportSize{ 0.0f };
    // Tracks whether the window is focused so we can route input to camera controls.
    bool m_IsFocused = false;
    // Tracks whether the window is hovered, used in combination with focus to enable tools.
    bool m_IsHovered = false;
    // Stores whether camera controls should be active for the current frame.
    bool m_IsCameraControlEnabled = false;
    // Camera entity currently driving the viewport render target.
    Trident::ECS::Entity m_ActiveCameraEntity = 0;
    // Shared gizmo configuration so this panel stays in sync with inspector controls.
    GizmoState* m_GizmoState = nullptr;
};