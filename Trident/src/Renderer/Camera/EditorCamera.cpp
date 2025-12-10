#include "Renderer/Camera/EditorCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <limits>

namespace Trident
{
    namespace
    {
        constexpr float s_MinimumOrthographicSize = 0.01f;
        constexpr float s_MinimumFieldOfView = 1.0f;
        constexpr float s_MaximumFieldOfView = 179.0f;
        constexpr float s_MinimumClipDistance = 0.001f;
    }

    EditorCamera::EditorCamera()
    {
        RecalculateOrientation();
        RecalculateProjectionMatrix();
        RecalculateViewMatrix();
    }

    const glm::mat4& EditorCamera::GetViewMatrix() const
    {
        return m_ViewMatrix;
    }

    const glm::mat4& EditorCamera::GetProjectionMatrix() const
    {
        return m_ProjectionMatrix;
    }

    void EditorCamera::SetPosition(const glm::vec3& position)
    {
        m_Position = position;
        RecalculateViewMatrix();
    }

    void EditorCamera::SetRotation(const glm::vec3& eulerDegrees)
    {
        m_Rotation = eulerDegrees;
        RecalculateOrientation(); // Only calculate sin/cos when rotation actually changes
        RecalculateViewMatrix();
    }

    void EditorCamera::SetProjectionType(ProjectionType type)
    {
        if (m_ProjectionType == type) return;
        m_ProjectionType = type;
        RecalculateProjectionMatrix();
    }

    void EditorCamera::SetFieldOfView(float fieldOfViewDegrees)
    {
        const float l_Clamped = glm::clamp(fieldOfViewDegrees, s_MinimumFieldOfView, s_MaximumFieldOfView);
        if (glm::epsilonEqual(m_FieldOfView, l_Clamped, std::numeric_limits<float>::epsilon())) return;

        m_FieldOfView = l_Clamped;
        RecalculateProjectionMatrix();
    }

    void EditorCamera::SetOrthographicSize(float size)
    {
        const float l_Clamped = std::max(size, s_MinimumOrthographicSize);
        if (glm::epsilonEqual(m_OrthographicSize, l_Clamped, std::numeric_limits<float>::epsilon())) return;

        m_OrthographicSize = l_Clamped;
        RecalculateProjectionMatrix();
    }

    void EditorCamera::SetClipPlanes(float nearClip, float farClip)
    {
        const float l_SanitisedNear = std::max(nearClip, s_MinimumClipDistance);
        const float l_SanitisedFar = std::max(farClip, l_SanitisedNear + s_MinimumClipDistance);

        if (glm::epsilonEqual(m_NearClip, l_SanitisedNear, std::numeric_limits<float>::epsilon()) &&
            glm::epsilonEqual(m_FarClip, l_SanitisedFar, std::numeric_limits<float>::epsilon()))
        {
            return;
        }

        m_NearClip = l_SanitisedNear;
        m_FarClip = l_SanitisedFar;
        RecalculateProjectionMatrix();
    }

    void EditorCamera::SetViewportSize(const glm::vec2& viewportSize)
    {
        glm::vec2 l_Size = viewportSize;
        if (l_Size.x <= 0.0f) l_Size.x = 1.0f;
        if (l_Size.y <= 0.0f) l_Size.y = 1.0f;

        if (glm::all(glm::epsilonEqual(m_ViewportSize, l_Size, std::numeric_limits<float>::epsilon()))) return;

        m_ViewportSize = l_Size;
        RecalculateProjectionMatrix();
    }

    void EditorCamera::Invalidate()
    {
        RecalculateOrientation();
        RecalculateProjectionMatrix();
        RecalculateViewMatrix();
    }

    glm::vec3 EditorCamera::GetForwardDirection() const
    {
        return glm::rotate(m_Orientation, glm::vec3(0.0f, 0.0f, -1.0f));
    }

    glm::vec3 EditorCamera::GetRightDirection() const
    {
        return glm::rotate(m_Orientation, glm::vec3(1.0f, 0.0f, 0.0f));
    }

    glm::vec3 EditorCamera::GetUpDirection() const
    {
        return glm::rotate(m_Orientation, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void EditorCamera::RecalculateOrientation()
    {
        m_Orientation = glm::quat(glm::radians(m_Rotation));
    }

    void EditorCamera::RecalculateViewMatrix()
    {
        // View Matrix = Inverse(Transform) = Inverse(Rotation * Translation)
        // Since we store position and rotation directly, we build the inverse components.
        const glm::mat4 l_Rotation = glm::mat4_cast(glm::conjugate(m_Orientation));
        const glm::mat4 l_Translation = glm::translate(glm::mat4(1.0f), -m_Position);
        m_ViewMatrix = l_Rotation * l_Translation;
    }

    void EditorCamera::RecalculateProjectionMatrix()
    {
        const float l_Aspect = std::max(m_ViewportSize.x / std::max(m_ViewportSize.y, 0.0001f), 0.0001f);

        if (m_ProjectionType == ProjectionType::Orthographic)
        {
            const float l_HalfHeight = m_OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;

            // Vulkan uses [0, 1] Z-range. glm::orthoRH_ZO handles this.
            m_ProjectionMatrix = glm::orthoRH_ZO(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, m_NearClip, m_FarClip);
        }
        else
        {
            // Use RH_ZO for correct Vulkan Depth (0 to 1) instead of standard GL (-1 to 1)
            m_ProjectionMatrix = glm::perspectiveRH_ZO(glm::radians(m_FieldOfView), l_Aspect, m_NearClip, m_FarClip);
        }

        // Vulkan Y-Flip: This is required for rendering but must be undone for Gizmos.
        m_ProjectionMatrix[1][1] *= -1.0f;
    }
}