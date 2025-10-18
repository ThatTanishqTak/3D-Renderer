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
#include <glm/vec3.hpp>

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

    // Unity-like helpers
    void FrameSelection();
    static glm::vec3 ForwardFromYawPitch(float yawDeg, float pitchDeg);

private:
    GizmoState m_GizmoState;
    ViewportPanel m_ViewportPanel;
    ContentBrowserPanel m_ContentBrowserPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;

    Trident::EditorCamera m_EditorCamera;           ///< Viewport camera providing authoring controls.

    // Orientation state (degrees)
    float m_EditorYawDegrees = -90.0f;              ///< Horizontal orbit angle stored in degrees for clarity.
    float m_EditorPitchDegrees = -20.0f;            ///< Vertical orbit angle clamped to avoid gimbal flips.

    // Translation speed
    float m_CameraMoveSpeed = 5.0f;                 ///< Baseline translation speed in world units per second.
    float m_CameraBoostMultiplier = 4.0f;           ///< Speed multiplier engaged while shift is held.

    // Mouse sensitivity
    float m_MouseRotationSpeed = 0.2f;              ///< Degrees rotated per pixel of mouse movement.
    float m_MouseZoomSpeed = 2.0f;                  ///< Distance moved along the forward vector per wheel notch.

    // Cursor tracking
    glm::vec2 m_LastCursorPosition{ 0.0f, 0.0f };   ///< Tracks previous cursor position to reset drag pivots.
    bool m_IsRotateOrbitActive = false;             ///< Flags whether the current frame is processing an orbit drag.
    bool m_ResetRotateOrbitReference = true;        ///< Ensures the next drag seeds from the current cursor location.
    glm::vec2 m_CurrentCursorPosition{ 0.0f, 0.0f };///< Last known cursor position reported by the windowing system.
    glm::vec2 m_PendingCursorDelta{ 0.0f, 0.0f };   ///< Accumulated mouse delta since the previous update tick.
    float m_PendingScrollDelta = 0.0f;              ///< Scroll wheel offset captured from recent events.
    bool m_HasCursorPosition = false;               ///< Indicates whether any cursor events have been observed yet.

    // Mouse/keyboard state
    bool m_IsRightMouseButtonDown = false;          ///< RMB for free-look fly.
    bool m_IsMiddleMouseButtonDown = false;         ///< MMB for pan.
    bool m_IsLeftMouseButtonDown = false;           ///< LMB used with Alt for orbit.

    bool m_IsShiftDown = false;                     ///< Shift boosts fly speed.
    bool m_IsAltDown = false;                       ///< Alt gates orbit/dolly modes.

    bool m_IsKeyWDown = false;                      ///< Forward
    bool m_IsKeyADown = false;                      ///< Strafe left
    bool m_IsKeySDown = false;                      ///< Backward
    bool m_IsKeyDDown = false;                      ///< Strafe right
    bool m_IsKeyQDown = false;                      ///< Down
    bool m_IsKeyEDown = false;                      ///< Up

    // Orbit + framing
    glm::vec3 m_CameraPivot{ 0.0f, 0.0f, 0.0f };    ///< Orbit center used by Alt+LMB and Frame (F).
    float m_OrbitDistance = 8.0f;               ///< Distance from pivot when orbiting.
    float m_MinOrbitDistance = 0.05f;           ///< Clamp to avoid flipping at the pivot.

    // Smoothing
    float m_PosSmoothing = 12.0f;               ///< Higher is snappier, 0 disables smoothing.
    float m_RotSmoothing = 14.0f;
    glm::vec3 m_TargetPosition{ 0.0f, 3.0f, 8.0f };
    float m_TargetYawDegrees = -90.0f;
    float m_TargetPitchDegrees = -20.0f;

    // Pan/dolly tuning
    float m_PanSpeedFactor = 1.0f;             ///< Scales with orbit distance for consistent feel.
    float m_DollySpeedFactor = 0.15f;               ///< Used for Alt+RMB/scroll while orbiting.

    // Fly speed limits
    float m_MinMoveSpeed = 10.1f;
    float m_MaxMoveSpeed = 50.0f;

    // One-shot requests
    bool  m_RequestFrameSelection = false;
};