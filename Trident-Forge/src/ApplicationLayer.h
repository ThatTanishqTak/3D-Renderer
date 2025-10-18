#pragma once

#include "Layer/Layer.h"
#include "Events/ApplicationEvents.h"
#include "Renderer/Camera/EditorCamera.h"

#include "Panels/ViewportPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/GizmoState.h"

#include <glm/vec2.hpp>

#include <string>
#include <vector>

class ApplicationLayer : public Trident::Layer
{
public:
    /**
     * Prepare application-specific resources (scenes, editor state, etc.).
     */
    void Initialize() override;
    /**
     * Release resources acquired during Initialize().
     */
    void Shutdown() override;

    /**
     * Execute per-frame simulation such as tools or gameplay logic.
     */
    void Update() override;
    /**
     * Submit draw calls and UI for the current frame.
     */
    void Render() override;
    /**
     * React to engine events, including file drops routed from the operating system.
     */
    void OnEvent(Trident::Events& event) override;

private:
    bool HandleFileDrop(Trident::FileDropEvent& event);
    bool ImportDroppedAssets(const std::vector<std::string>& droppedPaths);

    void UpdateEditorCamera(float a_DeltaTime);

private:
    GizmoState m_GizmoState;
    ViewportPanel m_ViewportPanel;
    ContentBrowserPanel m_ContentBrowserPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;

    Trident::EditorCamera m_EditorCamera;           ///< Viewport camera providing authoring controls.
    float m_EditorYawDegrees = -90.0f;              ///< Horizontal orbit angle stored in degrees for clarity.
    float m_EditorPitchDegrees = -20.0f;            ///< Vertical orbit angle clamped to avoid gimbal flips.
    float m_CameraMoveSpeed = 5.0f;                 ///< Baseline translation speed in world units per second.
    float m_CameraBoostMultiplier = 4.0f;           ///< Speed multiplier engaged while shift is held.
    float m_MouseRotationSpeed = 0.2f;              ///< Degrees rotated per pixel of mouse movement.
    float m_MouseZoomSpeed = 2.0f;                  ///< Distance moved along the forward vector per wheel notch.
    glm::vec2 m_LastCursorPosition{ 0.0f, 0.0f };   ///< Tracks previous cursor position to reset drag pivots.
    bool m_IsRotateOrbitActive = false;             ///< Flags whether the current frame is processing an orbit drag.
    bool m_ResetRotateOrbitReference = true;        ///< Ensures the next drag seeds from the current cursor location.
    glm::vec2 m_CurrentCursorPosition{ 0.0f, 0.0f }; ///< Last known cursor position reported by the windowing system.
    glm::vec2 m_PendingCursorDelta{ 0.0f, 0.0f };    ///< Accumulated mouse delta since the previous update tick.
    float m_PendingScrollDelta = 0.0f;               ///< Scroll wheel offset captured from recent events.
    bool m_HasCursorPosition = false;                ///< Indicates whether any cursor events have been observed yet.
    bool m_IsRightMouseButtonDown = false;           ///< Tracks the state of the right mouse button for orbit toggling.
    bool m_IsShiftDown = false;                      ///< Caches whether either shift key is currently pressed.
    bool m_IsKeyWDown = false;                       ///< Remembers if forward movement should be applied this frame.
    bool m_IsKeyADown = false;                       ///< Remembers if left strafe movement should be applied this frame.
    bool m_IsKeySDown = false;                       ///< Remembers if backward movement should be applied this frame.
    bool m_IsKeyDDown = false;                       ///< Remembers if right strafe movement should be applied this frame.
    bool m_IsKeyQDown = false;                       ///< Remembers if downward movement should be applied this frame.
    bool m_IsKeyEDown = false;                       ///< Remembers if upward movement should be applied this frame.
};