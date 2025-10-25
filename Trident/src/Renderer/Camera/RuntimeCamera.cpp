#include "Renderer/Camera/RuntimeCamera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/epsilon.hpp>

#include <cmath>
#include <limits>

namespace Trident
{
    RuntimeCamera::RuntimeCamera()
    {
        // Seed the projection cache so the renderer can consume the camera immediately after construction.
        UpdateProjectionMatrix();
        // The view cache is marked dirty by default; UpdateViewMatrix() will populate it on first use.
    }

    const glm::mat4& RuntimeCamera::GetViewMatrix() const
    {
        if (m_ViewDirty)
        {
            UpdateViewMatrix();
        }

        return m_ViewMatrix;
    }

    const glm::mat4& RuntimeCamera::GetProjectionMatrix() const
    {
        if (m_ProjectionDirty)
        {
            UpdateProjectionMatrix();
        }

        return m_ProjectionMatrix;
    }

    glm::vec3 RuntimeCamera::GetPosition() const
    {
        return m_Position;
    }

    glm::vec3 RuntimeCamera::GetRotation() const
    {
        return m_Rotation;
    }

    void RuntimeCamera::SetPosition(const glm::vec3& position)
    {
        constexpr float l_PositionTolerance = 0.0001f;
        const bool l_PositionUnchanged = glm::all(glm::epsilonEqual(m_Position, position, l_PositionTolerance));
        if (l_PositionUnchanged)
        {
            return;
        }

        m_Position = position;
        m_ViewDirty = true;
    }

    void RuntimeCamera::SetRotation(const glm::vec3& eulerDegrees)
    {
        constexpr float l_RotationTolerance = 0.0001f;
        const bool l_RotationUnchanged = glm::all(glm::epsilonEqual(m_Rotation, eulerDegrees, l_RotationTolerance));
        if (l_RotationUnchanged)
        {
            return;
        }

        m_Rotation = eulerDegrees;
        m_ViewDirty = true;
    }

    void RuntimeCamera::SetProjectionType(ProjectionType type)
    {
        if (m_ProjectionType == type)
        {
            return;
        }

        m_ProjectionType = type;
        m_ProjectionDirty = true;
    }

    void RuntimeCamera::SetFieldOfView(float fieldOfViewDegrees)
    {
        const bool l_FieldOfViewUnchanged = std::fabs(m_FieldOfView - fieldOfViewDegrees) <= std::numeric_limits<float>::epsilon();
        if (l_FieldOfViewUnchanged)
        {
            return;
        }

        m_FieldOfView = fieldOfViewDegrees;
        m_ProjectionDirty = true;
    }

    void RuntimeCamera::SetOrthographicSize(float size)
    {
        const bool l_SizeUnchanged = std::fabs(m_OrthographicSize - size) <= std::numeric_limits<float>::epsilon();
        if (l_SizeUnchanged)
        {
            return;
        }

        m_OrthographicSize = size;
        m_ProjectionDirty = true;
    }

    void RuntimeCamera::SetClipPlanes(float nearClip, float farClip)
    {
        const bool l_NearUnchanged = std::fabs(m_NearClip - nearClip) <= std::numeric_limits<float>::epsilon();
        const bool l_FarUnchanged = std::fabs(m_FarClip - farClip) <= std::numeric_limits<float>::epsilon();
        if (l_NearUnchanged && l_FarUnchanged)
        {
            return;
        }

        m_NearClip = nearClip;
        m_FarClip = farClip;
        m_ProjectionDirty = true;
    }

    void RuntimeCamera::SetViewportSize(const glm::vec2& viewportSize)
    {
        constexpr float l_SizeTolerance = 0.0001f;
        const bool l_SizeUnchanged = glm::all(glm::epsilonEqual(m_ViewportSize, viewportSize, l_SizeTolerance));
        if (l_SizeUnchanged)
        {
            return;
        }

        m_ViewportSize = viewportSize;
        m_ProjectionDirty = true;
    }

    void RuntimeCamera::Invalidate()
    {
        // Force the next call to GetViewMatrix()/GetProjectionMatrix() to rebuild cached transforms.
        m_ViewDirty = true;
        m_ProjectionDirty = true;
    }

    glm::vec3 RuntimeCamera::GetForwardDirection() const
    {
        const glm::quat l_Orientation = GetOrientation();
        return l_Orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    }

    glm::vec3 RuntimeCamera::GetRightDirection() const
    {
        const glm::quat l_Orientation = GetOrientation();
        return l_Orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 RuntimeCamera::GetUpDirection() const
    {
        const glm::quat l_Orientation = GetOrientation();
        return l_Orientation * glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::quat RuntimeCamera::GetOrientation() const
    {
        return BuildOrientation();
    }

    void RuntimeCamera::UpdateViewMatrix() const
    {
        const glm::quat l_Orientation = BuildOrientation();
        const glm::vec3 l_Forward = l_Orientation * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 l_Up = l_Orientation * glm::vec3(0.0f, 1.0f, 0.0f);

        const glm::vec3 l_FocusPoint = m_Position + l_Forward;
        m_ViewMatrix = glm::lookAt(m_Position, l_FocusPoint, l_Up);
        m_ViewDirty = false;
    }

    void RuntimeCamera::UpdateProjectionMatrix() const
    {
        const float l_AspectRatio = (m_ViewportSize.y > 0.0f) ? (m_ViewportSize.x / m_ViewportSize.y) : 1.0f;

        if (m_ProjectionType == ProjectionType::Perspective)
        {
            m_ProjectionMatrix = glm::perspective(glm::radians(m_FieldOfView), l_AspectRatio, m_NearClip, m_FarClip);
            // Vulkan's clip space has inverted Y, so correct the matrix before handing it to the renderer.
            m_ProjectionMatrix[1][1] *= -1.0f;
        }
        else
        {
            const float l_OrthoHeight = m_OrthographicSize;
            const float l_OrthoWidth = l_OrthoHeight * l_AspectRatio;
            m_ProjectionMatrix = glm::ortho(-l_OrthoWidth, l_OrthoWidth, -l_OrthoHeight, l_OrthoHeight, m_NearClip, m_FarClip);
        }

        m_ProjectionDirty = false;
    }

    glm::quat RuntimeCamera::BuildOrientation() const
    {
        // Compose the quaternion directly from Euler angles to stay in sync with editor camera conventions.
        const glm::vec3 l_EulerRadians = glm::radians(m_Rotation);
        glm::quat l_Orientation = glm::quat(l_EulerRadians);
        return glm::normalize(l_Orientation);
    }
}