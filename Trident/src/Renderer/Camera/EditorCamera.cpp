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
        constexpr float s_MinimumOrthographicSize = 0.01f; ///< Prevents degenerate volumes that break precision.
        constexpr float s_MinimumFieldOfView = 1.0f;        ///< Guards against invalid perspective projections.
        constexpr float s_MaximumFieldOfView = 179.0f;      ///< Prevents inverted frustums when approaching 180 degrees.
        constexpr float s_MinimumClipDistance = 0.001f;     ///< Protects against zero clip plane distances.
    }

    EditorCamera::EditorCamera()
    {
        // Default state matches the legacy free-fly behaviour so existing scenes remain usable.
        Invalidate();
    }

    const glm::mat4& EditorCamera::GetViewMatrix() const
    {
        if (m_ViewDirty)
        {
            UpdateViewMatrix();
        }

        return m_ViewMatrix;
    }

    const glm::mat4& EditorCamera::GetProjectionMatrix() const
    {
        if (m_ProjectionDirty)
        {
            UpdateProjectionMatrix();
        }

        return m_ProjectionMatrix;
    }

    glm::vec3 EditorCamera::GetPosition() const
    {
        return m_Position;
    }

    glm::vec3 EditorCamera::GetRotation() const
    {
        return m_Rotation;
    }

    void EditorCamera::SetPosition(const glm::vec3& position)
    {
        m_Position = position;
        m_ViewDirty = true;
    }

    void EditorCamera::SetRotation(const glm::vec3& eulerDegrees)
    {
        m_Rotation = eulerDegrees;
        m_ViewDirty = true;
    }

    void EditorCamera::SetProjectionType(ProjectionType type)
    {
        if (m_ProjectionType == type)
        {
            return;
        }

        m_ProjectionType = type;
        m_ProjectionDirty = true;
    }

    void EditorCamera::SetFieldOfView(float fieldOfViewDegrees)
    {
        const float l_Clamped = glm::clamp(fieldOfViewDegrees, s_MinimumFieldOfView, s_MaximumFieldOfView);
        if (glm::epsilonEqual(m_FieldOfView, l_Clamped, std::numeric_limits<float>::epsilon()))
        {
            return;
        }

        m_FieldOfView = l_Clamped;
        m_ProjectionDirty = true;
    }

    void EditorCamera::SetOrthographicSize(float size)
    {
        const float l_Clamped = std::max(size, s_MinimumOrthographicSize);
        if (glm::epsilonEqual(m_OrthographicSize, l_Clamped, std::numeric_limits<float>::epsilon()))
        {
            return;
        }

        m_OrthographicSize = l_Clamped;
        m_ProjectionDirty = true;
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
        m_ProjectionDirty = true;
    }

    void EditorCamera::SetViewportSize(const glm::vec2& viewportSize)
    {
        glm::vec2 l_Size = viewportSize;
        if (l_Size.x <= 0.0f)
        {
            l_Size.x = 1.0f;
        }
        if (l_Size.y <= 0.0f)
        {
            l_Size.y = 1.0f;
        }

        if (glm::all(glm::epsilonEqual(m_ViewportSize, l_Size, std::numeric_limits<float>::epsilon())))
        {
            return;
        }

        m_ViewportSize = l_Size;
        m_ProjectionDirty = true;
    }

    void EditorCamera::Invalidate()
    {
        // Reset cached matrices so any debug visualisation observes predictable identity data until recomputed.
        m_ViewMatrix = glm::mat4{ 1.0f };
        m_ProjectionMatrix = glm::mat4{ 1.0f };
        m_ViewDirty = true;
        m_ProjectionDirty = true;
    }

    glm::vec3 EditorCamera::GetForwardDirection() const
    {
        const glm::quat l_Orientation = BuildOrientation();
        const glm::vec3 l_Forward = l_Orientation * glm::vec3{ 0.0f, 0.0f, -1.0f };

        return glm::normalize(l_Forward);
    }

    glm::vec3 EditorCamera::GetRightDirection() const
    {
        const glm::quat l_Orientation = BuildOrientation();
        const glm::vec3 l_Right = l_Orientation * glm::vec3{ 1.0f, 0.0f, 0.0f };

        return glm::normalize(l_Right);
    }

    glm::vec3 EditorCamera::GetUpDirection() const
    {
        const glm::quat l_Orientation = BuildOrientation();
        const glm::vec3 l_Up = l_Orientation * glm::vec3{ 0.0f, 1.0f, 0.0f };

        return glm::normalize(l_Up);
    }

    glm::quat EditorCamera::GetOrientation() const
    {
        return BuildOrientation();
    }

    void EditorCamera::UpdateViewMatrix() const
    {
        const glm::quat l_Orientation = BuildOrientation();
        // Use the conjugate so the view matrix represents the inverse rotation (camera to world).
        const glm::mat4 l_Rotation = glm::mat4_cast(glm::conjugate(l_Orientation));
        const glm::mat4 l_Translation = glm::translate(glm::mat4{ 1.0f }, -m_Position);

        // Compose rotation and translation to build a typical look-at matrix.
        m_ViewMatrix = l_Rotation * l_Translation;
        m_ViewDirty = false;
    }

    void EditorCamera::UpdateProjectionMatrix() const
    {
        const float l_Aspect = std::max(m_ViewportSize.x / std::max(m_ViewportSize.y, 0.0001f), 0.0001f);

        if (m_ProjectionType == ProjectionType::Orthographic)
        {
            const float l_HalfHeight = m_OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;
            m_ProjectionMatrix = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, m_NearClip, m_FarClip);
        }
        else
        {
            m_ProjectionMatrix = glm::perspective(glm::radians(m_FieldOfView), l_Aspect, m_NearClip, m_FarClip);
        }

        // Vulkan requires a flipped Y axis in clip space.
        m_ProjectionMatrix[1][1] *= -1.0f;

        m_ProjectionDirty = false;
    }

    glm::quat EditorCamera::BuildOrientation() const
    {
        const glm::vec3 l_Radians = glm::radians(m_Rotation);
        const glm::quat l_Orientation = glm::quat(l_Radians);

        return l_Orientation;
    }
}