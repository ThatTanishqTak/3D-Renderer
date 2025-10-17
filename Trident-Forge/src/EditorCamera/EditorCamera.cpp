#include "EditorCamera.h"

#include "Renderer/RenderCommand.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr glm::vec3 s_WorldUp{ 0.0f, 0.0f, 1.0f };
}

EditorCamera::EditorCamera()
{
    // Start slightly elevated looking toward the origin for a helpful default editor view.
    SetPosition(glm::vec3{ 0.0f, -5.0f, 3.0f });
    // Provide a generous orthographic extent so the editor begins with a wide framing.
    SetOrthographicSize(20.0f);
    // Ensure the cached forward/right/up vectors reflect the initial yaw/pitch setup.
    UpdateCachedVectors();
    // Position the camera so the renderer picks up the default editor viewpoint immediately.
    UpdateRenderCamera();
}

void EditorCamera::Update(float deltaTime)
{
    (void)deltaTime;
    // Editor camera updates are event driven; no per-frame work is required yet.
}

void EditorCamera::SetInvertLook(bool invertLook)
{
    m_InvertLook = invertLook;
}

void EditorCamera::UpdateOrbit(const glm::vec2& mouseDelta, float deltaTime)
{
    const bool l_HasMovement = (mouseDelta.x != 0.0f) || (mouseDelta.y != 0.0f);
    if (!l_HasMovement)
    {
        // Decay the smoothing timer so future interpolation hooks can blend back to rest.
        m_OrbitSmoothingTimer = std::max(m_OrbitSmoothingTimer - deltaTime, 0.0f);
        return;
    }

    float l_YawDelta = mouseDelta.x * m_MouseSensitivity;
    float l_PitchDelta = mouseDelta.y * m_MouseSensitivity;
    if (!m_InvertLook)
    {
        l_PitchDelta = -l_PitchDelta;
    }

    m_YawDegrees += l_YawDelta;
    m_PitchDegrees += l_PitchDelta;
    ClampPitch();
    UpdateCachedVectors();

    // Keep the camera on the orbit sphere while looking toward the pivot.
    m_Position = m_OrbitPivot - (m_Forward * m_OrbitDistance);
    m_OrbitSmoothingTimer = m_SmoothingReset;
}

void EditorCamera::UpdatePan(const glm::vec2& mouseDelta, float deltaTime)
{
    const bool l_HasMovement = (mouseDelta.x != 0.0f) || (mouseDelta.y != 0.0f);
    if (!l_HasMovement)
    {
        m_PanSmoothingTimer = std::max(m_PanSmoothingTimer - deltaTime, 0.0f);
        return;
    }

    // Scale pan speed by the current orbit distance so precision improves when zoomed in.
    const float l_DistanceScale = std::max(m_OrbitDistance, 0.001f);
    glm::vec3 l_Delta = (-mouseDelta.x * m_Right + mouseDelta.y * m_Up) * m_PanSpeed * l_DistanceScale * deltaTime;
    m_Position += l_Delta;
    m_OrbitPivot += l_Delta;
    m_PanSmoothingTimer = m_SmoothingReset;
}

void EditorCamera::UpdateDolly(float scrollDelta, float deltaTime)
{
    if (scrollDelta == 0.0f)
    {
        m_DollySmoothingTimer = std::max(m_DollySmoothingTimer - deltaTime, 0.0f);

        return;
    }

    if (m_Projection == ProjectionType::Orthographic)
    {
        // Scale the orthographic volume instead of changing the orbit radius to mimic DCC behaviour.
        const float l_ScaleFactor = std::exp(scrollDelta * 0.08f);
        const float l_MinimumSize = 0.01f;
        const float l_MaximumSize = 10000.0f;
        m_OrthographicSize = std::clamp(m_OrthographicSize * l_ScaleFactor, l_MinimumSize, l_MaximumSize);
        m_DollySmoothingTimer = m_SmoothingReset;

        return;
    }

    // Positive deltas increase the distance while negative deltas close toward the pivot.
    const float l_DollyOffset = scrollDelta * m_DollySpeed * std::max(m_OrbitDistance, 0.001f) * deltaTime;
    m_OrbitDistance = std::max(m_OrbitDistance + l_DollyOffset, 0.05f);
    m_Position = m_OrbitPivot - (m_Forward * m_OrbitDistance);
    m_DollySmoothingTimer = m_SmoothingReset;
}

void EditorCamera::UpdateMouseLook(const glm::vec2& mouseDelta, float deltaTime)
{
    const bool l_HasMovement = (mouseDelta.x != 0.0f) || (mouseDelta.y != 0.0f);
    if (!l_HasMovement)
    {
        m_OrbitSmoothingTimer = std::max(m_OrbitSmoothingTimer - deltaTime, 0.0f);
        return;
    }

    float l_YawDelta = mouseDelta.x * m_MouseSensitivity;
    float l_PitchDelta = mouseDelta.y * m_MouseSensitivity;
    if (!m_InvertLook)
    {
        l_PitchDelta = -l_PitchDelta;
    }

    m_YawDegrees += l_YawDelta;
    m_PitchDegrees += l_PitchDelta;
    ClampPitch();
    UpdateCachedVectors();

    // Keep the orbit pivot in front of the camera so future orbit operations remain stable.
    m_OrbitPivot = m_Position + (m_Forward * m_OrbitDistance);
    m_OrbitSmoothingTimer = m_SmoothingReset;
}

void EditorCamera::UpdateFly(const glm::vec3& localDirection, float deltaTime, bool boostActive)
{
    if (localDirection.x == 0.0f && localDirection.y == 0.0f && localDirection.z == 0.0f)
    {
        m_FlySmoothingTimer = std::max(m_FlySmoothingTimer - deltaTime, 0.0f);
        return;
    }

    glm::vec3 l_Translation = (m_Right * localDirection.x) + (m_Forward * localDirection.y) + (s_WorldUp * localDirection.z);
    if (glm::length(l_Translation) > 0.0f)
    {
        l_Translation = glm::normalize(l_Translation);
    }

    float l_Speed = m_FlySpeed;
    if (boostActive)
    {
        l_Speed *= m_SpeedBoostMultiplier;
    }

    m_Position += l_Translation * l_Speed * deltaTime;
    m_OrbitPivot = m_Position + (m_Forward * m_OrbitDistance);
    m_FlySmoothingTimer = m_SmoothingReset;
}

void EditorCamera::SetOrbitPivot(const glm::vec3& pivotPosition)
{
    const glm::vec3 l_CurrentOffset = m_Position - m_OrbitPivot;
    m_OrbitPivot = pivotPosition;
    m_Position = m_OrbitPivot + l_CurrentOffset;
    m_OrbitDistance = std::max(glm::length(l_CurrentOffset), 0.05f);
}

void EditorCamera::FrameTarget(const glm::vec3& targetPosition, float distance)
{
    m_OrbitPivot = targetPosition;
    m_OrbitDistance = std::max(distance, 0.05f);

    // Aim directly at the target so the frame centres the selection.
    glm::vec3 l_Direction = glm::normalize(m_OrbitPivot - m_Position);
    if (glm::length(l_Direction) < 0.0001f)
    {
        l_Direction = glm::normalize(glm::vec3{ 0.0f, 1.0f, 0.0f });
    }

    m_YawDegrees = glm::degrees(std::atan2(l_Direction.y, l_Direction.x));
    m_PitchDegrees = glm::degrees(std::asin(glm::clamp(l_Direction.z, -1.0f, 1.0f)));
    ClampPitch();
    UpdateCachedVectors();
    m_Position = m_OrbitPivot - (m_Forward * m_OrbitDistance);
}

void EditorCamera::AdjustFlySpeed(float scrollDelta)
{
    if (scrollDelta == 0.0f)
    {
        return;
    }

    const float l_Scale = 1.0f + (0.1f * scrollDelta);
    m_FlySpeed = std::clamp(m_FlySpeed * l_Scale, 0.1f, 500.0f);
}

void EditorCamera::SetFlySpeed(float a_Speed)
{
    m_FlySpeed = std::clamp(a_Speed, 0.1f, 500.0f);
}

void EditorCamera::SyncToRuntimeCamera(const glm::vec3& position, float yawDegrees, float pitchDegrees, float fieldOfViewDegrees)
{
    m_Position = position;
    m_YawDegrees = yawDegrees;
    m_PitchDegrees = pitchDegrees;
    m_FieldOfViewDegrees = fieldOfViewDegrees;
    ClampPitch();
    UpdateCachedVectors();

    // Reset the pivot so editor orbit controls continue smoothly from the runtime state.
    m_OrbitPivot = m_Position + (m_Forward * m_OrbitDistance);
    m_OrbitDistance = std::max(glm::length(m_OrbitPivot - m_Position), 0.05f);
    UpdateRenderCamera();
}

void EditorCamera::UpdateRenderCamera()
{
    // Push the latest editor camera transform into the renderer's default camera slot.
    Trident::RenderCommand::UpdateEditorCamera(m_Position, m_YawDegrees, m_PitchDegrees, m_FieldOfViewDegrees);
    Trident::RenderCommand::SetViewportProjection(m_Projection, m_OrthographicSize);
}

void EditorCamera::SnapToDirection(const glm::vec3& targetForward, const glm::vec3& preferredUp)
{
    const glm::vec3 l_NormalisedForward = glm::normalize(targetForward);
    if (glm::length(l_NormalisedForward) < 0.0001f)
    {
        // Degenerate input leave the camera unmodified to avoid erratic jumps.
        return;
    }

    glm::vec3 l_DesiredUp = glm::normalize(preferredUp);
    if (glm::length(l_DesiredUp) < 0.0001f || std::abs(glm::dot(l_DesiredUp, l_NormalisedForward)) > 0.999f)
    {
        // When the provided up vector is unusable, fall back to the cached camera up axis.
        l_DesiredUp = m_Up;
        if (std::abs(glm::dot(l_DesiredUp, l_NormalisedForward)) > 0.999f)
        {
            l_DesiredUp = s_WorldUp;
        }
    }

    const glm::vec3 l_Right = glm::normalize(glm::cross(l_NormalisedForward, l_DesiredUp));
    const glm::vec3 l_ReprojectedUp = glm::normalize(glm::cross(l_Right, l_NormalisedForward));

    m_YawDegrees = glm::degrees(std::atan2(l_NormalisedForward.y, l_NormalisedForward.x));
    m_PitchDegrees = glm::degrees(std::asin(glm::clamp(l_NormalisedForward.z, -1.0f, 1.0f)));
    ClampPitch();
    UpdateCachedVectors();

    // Overwrite the cached axes with the recomputed orthonormal basis so gizmos remain stable even at singularities.
    m_Forward = l_NormalisedForward;
    m_Right = l_Right;
    m_Up = l_ReprojectedUp;

    m_Position = m_OrbitPivot - (m_Forward * m_OrbitDistance);

    m_OrbitSmoothingTimer = 0.0f;
    m_PanSmoothingTimer = 0.0f;
    m_DollySmoothingTimer = 0.0f;
    m_FlySmoothingTimer = 0.0f;

    UpdateRenderCamera();
}

void EditorCamera::SetProjection(ProjectionType projection)
{
    if (m_Projection == projection)
    {
        return;
    }

    m_Projection = projection;
    Trident::Camera::SetProjection(projection);
    if (m_Projection == ProjectionType::Orthographic)
    {
        // Derive a reasonable starting frustum from the current orbit radius so the snap feels natural.
        const float l_DefaultSize = std::max(m_OrbitDistance * 2.0f, 0.1f);
        m_OrthographicSize = std::max(m_OrthographicSize, l_DefaultSize);
        Trident::Camera::SetOrthographicSize(m_OrthographicSize);
    }

    UpdateRenderCamera();
}

void EditorCamera::ToggleProjection()
{
    if (m_Projection == ProjectionType::Perspective)
    {
        SetProjection(ProjectionType::Orthographic);
    }
    else
    {
        SetProjection(ProjectionType::Perspective);
    }
}

void EditorCamera::SetOrthographicSize(float size)
{
    const float l_MinimumSize = 0.01f;
    const float l_MaximumSize = 10000.0f;
    m_OrthographicSize = std::clamp(size, l_MinimumSize, l_MaximumSize);
    Trident::Camera::SetOrthographicSize(m_OrthographicSize);
}

void EditorCamera::UpdateCachedVectors()
{
    // Reuse the shared helper to maintain parity with the runtime controller's yaw/pitch logic.
    UpdateCachedDirectionsFromAngles();

    if (glm::length(m_Right) < 0.0001f)
    {
        // When the forward vector aligns with world up we fallback to a canonical right.
        m_Right = glm::vec3{ 1.0f, 0.0f, 0.0f };
        m_Up = glm::normalize(glm::cross(m_Right, m_Forward));
    }
}