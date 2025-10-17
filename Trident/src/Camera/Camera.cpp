#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Trident
{
    glm::mat4 Camera::GetViewMatrix() const
    {
        // Build the view matrix from the cached basis vectors.
        return glm::lookAt(m_Position, m_Position + m_Forward, m_Up);
    }

    void Camera::SetPosition(const glm::vec3& a_Position)
    {
        // Allow tooling panels to teleport the camera without waiting for controller input.
        m_Position = a_Position;
    }

    void Camera::SetYaw(float a_YawDegrees)
    {
        // Update the horizontal angle so derived cameras stay aligned with editor controls.
        m_YawDegrees = a_YawDegrees;
        UpdateCachedDirectionsFromAngles();
    }

    void Camera::SetPitch(float a_PitchDegrees)
    {
        // Clamp vertical rotation to avoid flipping the camera upside down while editing.
        m_PitchDegrees = a_PitchDegrees;
        ClampPitch();
        UpdateCachedDirectionsFromAngles();
    }

    void Camera::SetFOV(float a_FieldOfViewDegrees)
    {
        // Keep the field of view within a comfortable perspective range for editors.
        m_FieldOfViewDegrees = std::clamp(a_FieldOfViewDegrees, 1.0f, 120.0f);
    }

    void Camera::SetNearClip(float a_NearClip)
    {
        // Prevent invalid projections by keeping the near plane in front of the far plane.
        const float l_ClampedNear = std::clamp(a_NearClip, 0.001f, m_FarClip - 0.001f);
        m_NearClip = l_ClampedNear;
    }

    void Camera::SetFarClip(float a_FarClip)
    {
        // Ensure the far plane always sits behind the near plane to avoid depth precision issues.
        const float l_MinFar = m_NearClip + 0.001f;
        m_FarClip = std::max(a_FarClip, l_MinFar);
    }

    void Camera::SetProjection(ProjectionType a_Projection)
    {
        // Persist the preferred projection so editor widgets and the renderer use the same frustum type.
        m_Projection = a_Projection;
    }

    void Camera::SetOrthographicSize(float a_Size)
    {
        // Prevent a degenerate frustum by clamping the orthographic extent to a small positive value.
        const float l_MinimumSize = 0.001f;
        m_OrthographicSize = std::max(a_Size, l_MinimumSize);
    }

    void Camera::UpdateCachedDirectionsFromAngles()
    {
        // Derive the camera basis vectors from yaw and pitch so both editor and runtime stay consistent.
        glm::vec3 l_Forward{};
        l_Forward.x = cos(glm::radians(m_YawDegrees)) * cos(glm::radians(m_PitchDegrees));
        l_Forward.y = sin(glm::radians(m_YawDegrees)) * cos(glm::radians(m_PitchDegrees));
        l_Forward.z = sin(glm::radians(m_PitchDegrees));

        m_Forward = glm::normalize(l_Forward);
        m_Right = glm::normalize(glm::cross(m_Forward, glm::vec3{ 0.0f, 0.0f, 1.0f }));
        m_Up = glm::normalize(glm::cross(m_Right, m_Forward));
    }

    void Camera::ClampPitch()
    {
        // Protect against gimbal lock by constraining the pitch to a sensible range.
        m_PitchDegrees = std::clamp(m_PitchDegrees, -89.0f, 89.0f);
    }
}