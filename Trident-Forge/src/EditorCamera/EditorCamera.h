#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// The EditorCamera encapsulates editor-specific camera behaviour such as orbiting,
// panning, dollying, and free-flight controls. The controller keeps a mirror of the
// renderer camera state so UI code can operate independently of runtime entities.
class EditorCamera
{
public:
    EditorCamera();

    // Configure whether vertical mouse motion should be inverted when mousing-look or orbiting.
    void SetInvertLook(bool a_InvertLook);
    bool IsLookInverted() const { return m_InvertLook; }

    // Update yaw/pitch so the camera orbits around the cached pivot.
    void UpdateOrbit(const glm::vec2& a_MouseDelta, float a_DeltaTime);
    // Translate the camera and pivot together for track-style movement.
    void UpdatePan(const glm::vec2& a_MouseDelta, float a_DeltaTime);
    // Adjust the orbit radius for dolly/zoom interactions while maintaining orientation.
    void UpdateDolly(float a_ScrollDelta, float a_DeltaTime);
    // Update yaw/pitch in place without modifying the orbit radius (free look mode).
    void UpdateMouseLook(const glm::vec2& a_MouseDelta, float a_DeltaTime);
    // Translate the camera in local space using WASD/QE style inputs.
    void UpdateFly(const glm::vec3& a_LocalDirection, float a_DeltaTime, bool a_BoostActive);

    // Adjust the pivot directly so selection changes stay centred.
    void SetOrbitPivot(const glm::vec3& a_PivotPosition);
    const glm::vec3& GetOrbitPivot() const { return m_OrbitPivot; }

    // Frame the supplied target by repositioning the camera at a requested distance.
    void FrameTarget(const glm::vec3& a_TargetPosition, float a_Distance);

    // Allow wheel shortcuts to grow/shrink the base fly speed.
    void AdjustFlySpeed(float a_ScrollDelta);
    void SetFlySpeed(float a_Speed);
    float GetFlySpeed() const { return m_FlySpeed; }

    // Update the cached transform from a runtime camera snapshot so tooling stays in sync.
    void SyncToRuntimeCamera(const glm::vec3& a_Position, float a_YawDegrees, float a_PitchDegrees, float a_FieldOfViewDegrees);

    // Push the currently stored transform back into the renderer.
    void UpdateRenderCamera();

    const glm::vec3& GetPosition() const { return m_Position; }
    float GetYaw() const { return m_YawDegrees; }
    float GetPitch() const { return m_PitchDegrees; }
    float GetFieldOfView() const { return m_FieldOfViewDegrees; }

private:
    void ClampPitch();
    void UpdateCachedVectors();

    glm::vec3 m_Position{ 0.0f, -5.0f, 3.0f }; // Start slightly elevated looking toward the origin.
    float m_YawDegrees = 90.0f;
    float m_PitchDegrees = -25.0f;

    glm::vec3 m_Forward{ 0.0f, 1.0f, 0.0f };
    glm::vec3 m_Right{ 1.0f, 0.0f, 0.0f };
    glm::vec3 m_Up{ 0.0f, 0.0f, 1.0f };

    glm::vec3 m_OrbitPivot{ 0.0f, 0.0f, 0.0f };
    float m_OrbitDistance = 8.0f;

    float m_MouseSensitivity = 0.12f;
    float m_PanSpeed = 1.0f;
    float m_DollySpeed = 6.0f;
    float m_FlySpeed = 5.0f;
    float m_SpeedBoostMultiplier = 4.0f;
    float m_FieldOfViewDegrees = 45.0f;

    bool m_InvertLook = false;

    // Simple timers that allow future smoothing/interpolation improvements.
    float m_OrbitSmoothingTimer = 0.0f;
    float m_PanSmoothingTimer = 0.0f;
    float m_DollySmoothingTimer = 0.0f;
    float m_FlySmoothingTimer = 0.0f;
    const float m_SmoothingReset = 0.25f;
};