#include "Renderer/Camera/RuntimeCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace Trident
{
    namespace
    {
        constexpr float s_MinimumClipDistance = 0.001f; ///< Prevents zero clip planes during authoring.
        constexpr float s_MinimumOrthographicSize = 0.01f; ///< Guards against degenerate orthographic frusta.
    }

    RuntimeCamera::RuntimeCamera(ECS::Registry& registry, ECS::Entity entity)
    {
        SetRegistry(&registry);
        SetEntity(entity);
    }

    void RuntimeCamera::SetRegistry(ECS::Registry* registry)
    {
        m_Registry = registry;
        Invalidate();
    }

    void RuntimeCamera::SetEntity(ECS::Entity entity)
    {
        m_Entity = entity;
        Invalidate();
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
        const Transform* l_Transform = GetTransformComponent();
        if (!l_Transform)
        {
            return glm::vec3{ 0.0f };
        }

        return l_Transform->Position;
    }

    glm::vec3 RuntimeCamera::GetRotation() const
    {
        const Transform* l_Transform = GetTransformComponent();
        if (!l_Transform)
        {
            return glm::vec3{ 0.0f };
        }

        return l_Transform->Rotation;
    }

    void RuntimeCamera::SetPosition(const glm::vec3& position)
    {
        Transform* l_Transform = GetTransformComponent();
        if (!l_Transform)
        {
            if (m_Registry)
            {
                l_Transform = &m_Registry->AddComponent<Transform>(m_Entity);
            }
        }

        if (!l_Transform)
        {
            return;
        }

        l_Transform->Position = position;
        m_ViewDirty = true;
    }

    void RuntimeCamera::SetRotation(const glm::vec3& eulerDegrees)
    {
        Transform* l_Transform = GetTransformComponent();
        if (!l_Transform)
        {
            if (m_Registry)
            {
                l_Transform = &m_Registry->AddComponent<Transform>(m_Entity);
            }
        }

        if (!l_Transform)
        {
            return;
        }

        l_Transform->Rotation = eulerDegrees;
        m_ViewDirty = true;
    }

    void RuntimeCamera::SetProjectionType(ProjectionType type)
    {
        CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return;
        }

        if (l_Component->m_ProjectionType == type)
        {
            return;
        }

        l_Component->m_ProjectionType = type;
        m_ProjectionDirty = true;
    }

    Camera::ProjectionType RuntimeCamera::GetProjectionType() const
    {
        const CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return Camera::ProjectionType::Perspective;
        }

        return l_Component->m_ProjectionType;
    }

    void RuntimeCamera::SetFieldOfView(float fieldOfViewDegrees)
    {
        CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return;
        }

        const float l_Clamped = std::clamp(fieldOfViewDegrees, 1.0f, 179.0f);
        if (glm::epsilonEqual(l_Component->m_FieldOfView, l_Clamped, std::numeric_limits<float>::epsilon()))
        {
            return;
        }

        l_Component->m_FieldOfView = l_Clamped;
        m_ProjectionDirty = true;
    }

    float RuntimeCamera::GetFieldOfView() const
    {
        const CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return 60.0f;
        }

        return l_Component->m_FieldOfView;
    }

    void RuntimeCamera::SetOrthographicSize(float size)
    {
        CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return;
        }

        const float l_Clamped = std::max(size, s_MinimumOrthographicSize);
        if (glm::epsilonEqual(l_Component->m_OrthographicSize, l_Clamped, std::numeric_limits<float>::epsilon()))
        {
            return;
        }

        l_Component->m_OrthographicSize = l_Clamped;
        m_ProjectionDirty = true;
    }

    float RuntimeCamera::GetOrthographicSize() const
    {
        const CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return 20.0f;
        }

        return l_Component->m_OrthographicSize;
    }

    void RuntimeCamera::SetClipPlanes(float nearClip, float farClip)
    {
        CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return;
        }

        const float l_SanitisedNear = std::max(nearClip, s_MinimumClipDistance);
        const float l_SanitisedFar = std::max(farClip, l_SanitisedNear + s_MinimumClipDistance);

        if (glm::epsilonEqual(l_Component->m_NearClip, l_SanitisedNear, std::numeric_limits<float>::epsilon()) &&
            glm::epsilonEqual(l_Component->m_FarClip, l_SanitisedFar, std::numeric_limits<float>::epsilon()))
        {
            return;
        }

        l_Component->m_NearClip = l_SanitisedNear;
        l_Component->m_FarClip = l_SanitisedFar;
        m_ProjectionDirty = true;
    }

    float RuntimeCamera::GetNearClip() const
    {
        const CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return 0.1f;
        }

        return l_Component->m_NearClip;
    }

    float RuntimeCamera::GetFarClip() const
    {
        const CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            return 1000.0f;
        }

        return l_Component->m_FarClip;
    }

    void RuntimeCamera::SetViewportSize(const glm::vec2& viewportSize)
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

    void RuntimeCamera::Invalidate()
    {
        m_ViewDirty = true;
        m_ProjectionDirty = true;
    }

    CameraComponent* RuntimeCamera::GetCameraComponent()
    {
        if (!m_Registry)
        {
            return nullptr;
        }

        if (!m_Registry->HasComponent<CameraComponent>(m_Entity))
        {
            return &m_Registry->AddComponent<CameraComponent>(m_Entity);
        }

        return &m_Registry->GetComponent<CameraComponent>(m_Entity);
    }

    const CameraComponent* RuntimeCamera::GetCameraComponent() const
    {
        if (!m_Registry || !m_Registry->HasComponent<CameraComponent>(m_Entity))
        {
            return nullptr;
        }

        return &m_Registry->GetComponent<CameraComponent>(m_Entity);
    }

    Transform* RuntimeCamera::GetTransformComponent()
    {
        if (!m_Registry)
        {
            return nullptr;
        }

        if (!m_Registry->HasComponent<Transform>(m_Entity))
        {
            return &m_Registry->AddComponent<Transform>(m_Entity);
        }

        return &m_Registry->GetComponent<Transform>(m_Entity);
    }

    const Transform* RuntimeCamera::GetTransformComponent() const
    {
        if (!m_Registry || !m_Registry->HasComponent<Transform>(m_Entity))
        {
            return nullptr;
        }

        return &m_Registry->GetComponent<Transform>(m_Entity);
    }

    void RuntimeCamera::UpdateViewMatrix() const
    {
        const Transform* l_Transform = GetTransformComponent();
        if (!l_Transform)
        {
            m_ViewMatrix = glm::mat4{ 1.0f };
            m_ViewDirty = false;
            return;
        }

        const glm::vec3 l_Radians = glm::radians(l_Transform->Rotation);
        glm::quat l_Orientation = glm::quat(l_Radians);

        // Normalize the quaternion so accumulated numerical error does not skew the view matrix.
        const float l_Length = glm::length(l_Orientation);
        if (l_Length > std::numeric_limits<float>::epsilon())
        {
            l_Orientation = glm::normalize(l_Orientation);
        }
        else
        {
            // Fall back to identity orientation if the quaternion becomes denormalized.
            l_Orientation = glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f };
        }

        // Use the conjugate so the view matrix applies the inverse rotation, matching editor camera behavior.
        const glm::mat4 l_Rotation = glm::mat4_cast(glm::conjugate(l_Orientation));
        const glm::mat4 l_Translation = glm::translate(glm::mat4{ 1.0f }, -l_Transform->Position);
        m_ViewMatrix = l_Rotation * l_Translation;
        m_ViewDirty = false;
    }

    void RuntimeCamera::UpdateProjectionMatrix() const
    {
        const CameraComponent* l_Component = GetCameraComponent();
        if (!l_Component)
        {
            m_ProjectionMatrix = glm::mat4{ 1.0f };
            m_ProjectionDirty = false;
            return;
        }

        const float l_RawAspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 0.0001f);
        const ProjectionType l_ProjectionType = l_Component->m_ProjectionType;

        if (l_ProjectionType == ProjectionType::Perspective)
        {
            // Respect the component's explicit aspect ratio when requested so cinematic cameras stay locked to authored values.
            const float l_EffectiveAspect = l_Component->m_FixedAspectRatio ? std::max(l_Component->m_AspectRatio, 0.0001f) : std::max(l_RawAspect, 0.0001f);
            const float l_FieldOfViewRadians = glm::radians(l_Component->m_FieldOfView);
            m_ProjectionMatrix = glm::perspective(l_FieldOfViewRadians, l_EffectiveAspect, l_Component->m_NearClip, l_Component->m_FarClip);

#ifndef NDEBUG
            // Validate that perspective projections continue to honour viewport aspect changes when the ratio is not fixed.
            const float l_TanHalfFov = std::tan(l_FieldOfViewRadians * 0.5f);
            if (!glm::epsilonEqual(l_TanHalfFov, 0.0f, 0.0001f))
            {
                const float l_ExpectedX = 1.0f / (l_EffectiveAspect * l_TanHalfFov);
                assert(glm::epsilonEqual(m_ProjectionMatrix[0][0], l_ExpectedX, 0.0005f) && "Perspective frustum lost aspect scaling after viewport resize");
            }
#endif
        }
        else
        {
            // Orthographic projections must also differentiate between runtime and fixed aspect calculations.
            const float l_EffectiveAspect = l_Component->m_FixedAspectRatio ? std::max(l_Component->m_AspectRatio, 0.0001f) : std::max(l_RawAspect, 0.0001f);
            const float l_HalfHeight = l_Component->m_OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_EffectiveAspect;
            m_ProjectionMatrix = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, l_Component->m_NearClip, l_Component->m_FarClip);

#ifndef NDEBUG
            // Confirm orthographic frusta keep the correct width when the viewport is resized.
            if (!glm::epsilonEqual(l_HalfWidth, 0.0f, 0.0001f))
            {
                const float l_ExpectedX = 1.0f / l_HalfWidth;
                assert(glm::epsilonEqual(m_ProjectionMatrix[0][0], l_ExpectedX, 0.0005f) && "Orthographic frustum lost aspect scaling after viewport resize");
            }
#endif
        }

#ifndef NDEBUG
        // Guard against regressions where fixed aspect ratios produced the wrong projection type when toggled at runtime.
        const bool l_IsPerspectiveMatrix = glm::epsilonEqual(m_ProjectionMatrix[3][3], 0.0f, 0.0001f);
        const bool l_IsOrthographicMatrix = glm::epsilonEqual(m_ProjectionMatrix[3][3], 1.0f, 0.0001f);
        assert((l_ProjectionType == ProjectionType::Perspective && l_IsPerspectiveMatrix) ||
            (l_ProjectionType == ProjectionType::Orthographic && l_IsOrthographicMatrix) &&
            "Runtime camera projection matrix does not match the component's projection type after toggling the fixed aspect ratio flag.");
#endif

        // Vulkan clip space is inverted on the Y axis.
        m_ProjectionMatrix[1][1] *= -1.0f;
        m_ProjectionDirty = false;
    }
}