#include "EditorCamera.h"

#include "Renderer/RenderCommand.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>

namespace
{
    constexpr glm::vec3 s_WorldUp{ 0.0f, 0.0f, 1.0f };
}

EditorCamera::EditorCamera()
{
    // Ensure the cached forward/right/up vectors reflect the initial yaw/pitch setup.
    UpdateCachedVectors();
    // Position the camera so the renderer picks up the default editor viewpoint immediately.
    UpdateRenderCamera();
}

void EditorCamera::SetInvertLook(bool a_InvertLook)
{
    m_InvertLook = a_InvertLook;
}

void EditorCamera::UpdateOrbit(const glm::vec2& a_MouseDelta, float a_DeltaTime)
{
    const bool l_HasMovement = (a_MouseDelta.x != 0.0f) || (a_MouseDelta.y != 0.0f);
    if (!l_HasMovement)
    {
        // Decay the smoothing timer so future interpolation hooks can blend back to rest.
        m_OrbitSmoothingTimer = std::max(m_OrbitSmoothingTimer - a_DeltaTime, 0.0f);
        return;
    }

    float l_YawDelta = a_MouseDelta.x * m_MouseSensitivity;
    float l_PitchDelta = a_MouseDelta.y * m_MouseSensitivity;
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

void EditorCamera::UpdatePan(const glm::vec2& a_MouseDelta, float a_DeltaTime)
{
    const bool l_HasMovement = (a_MouseDelta.x != 0.0f) || (a_MouseDelta.y != 0.0f);
    if (!l_HasMovement)
    {
        m_PanSmoothingTimer = std::max(m_PanSmoothingTimer - a_DeltaTime, 0.0f);
        return;
    }

    // Scale pan speed by the current orbit distance so precision improves when zoomed in.
    const float l_DistanceScale = std::max(m_OrbitDistance, 0.001f);
    glm::vec3 l_Delta = (-a_MouseDelta.x * m_Right + a_MouseDelta.y * m_Up) * m_PanSpeed * l_DistanceScale * a_DeltaTime;
    m_Position += l_Delta;
    m_OrbitPivot += l_Delta;
    m_PanSmoothingTimer = m_SmoothingReset;
}

void EditorCamera::UpdateDolly(float a_ScrollDelta, float a_DeltaTime)
{
    if (a_ScrollDelta == 0.0f)
    {
        m_DollySmoothingTimer = std::max(m_DollySmoothingTimer - a_DeltaTime, 0.0f);
        return;
    }

    // Positive deltas increase the distance while negative deltas close toward the pivot.
    const float l_DollyOffset = a_ScrollDelta * m_DollySpeed * std::max(m_OrbitDistance, 0.001f) * a_DeltaTime;
    m_OrbitDistance = std::max(m_OrbitDistance + l_DollyOffset, 0.05f);
    m_Position = m_OrbitPivot - (m_Forward * m_OrbitDistance);
    m_DollySmoothingTimer = m_SmoothingReset;
}

void EditorCamera::UpdateMouseLook(const glm::vec2& a_MouseDelta, float a_DeltaTime)
{
    const bool l_HasMovement = (a_MouseDelta.x != 0.0f) || (a_MouseDelta.y != 0.0f);
    if (!l_HasMovement)
    {
        m_OrbitSmoothingTimer = std::max(m_OrbitSmoothingTimer - a_DeltaTime, 0.0f);
        return;
    }

    float l_YawDelta = a_MouseDelta.x * m_MouseSensitivity;
    float l_PitchDelta = a_MouseDelta.y * m_MouseSensitivity;
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

void EditorCamera::UpdateFly(const glm::vec3& a_LocalDirection, float a_DeltaTime, bool a_BoostActive)
{
    if (a_LocalDirection.x == 0.0f && a_LocalDirection.y == 0.0f && a_LocalDirection.z == 0.0f)
    {
        m_FlySmoothingTimer = std::max(m_FlySmoothingTimer - a_DeltaTime, 0.0f);
        return;
    }

    glm::vec3 l_Translation = (m_Right * a_LocalDirection.x) + (m_Forward * a_LocalDirection.y) + (s_WorldUp * a_LocalDirection.z);
    if (glm::length(l_Translation) > 0.0f)
    {
        l_Translation = glm::normalize(l_Translation);
    }

    float l_Speed = m_FlySpeed;
    if (a_BoostActive)
    {
        l_Speed *= m_SpeedBoostMultiplier;
    }

    m_Position += l_Translation * l_Speed * a_DeltaTime;
    m_OrbitPivot = m_Position + (m_Forward * m_OrbitDistance);
    m_FlySmoothingTimer = m_SmoothingReset;
}

void EditorCamera::SetOrbitPivot(const glm::vec3& a_PivotPosition)
{
    const glm::vec3 l_CurrentOffset = m_Position - m_OrbitPivot;
    m_OrbitPivot = a_PivotPosition;
    m_Position = m_OrbitPivot + l_CurrentOffset;
    m_OrbitDistance = std::max(glm::length(l_CurrentOffset), 0.05f);
}

void EditorCamera::FrameTarget(const glm::vec3& a_TargetPosition, float a_Distance)
{
    m_OrbitPivot = a_TargetPosition;
    m_OrbitDistance = std::max(a_Distance, 0.05f);

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

void EditorCamera::AdjustFlySpeed(float a_ScrollDelta)
{
    if (a_ScrollDelta == 0.0f)
    {
        return;
    }

    const float l_Scale = 1.0f + (0.1f * a_ScrollDelta);
    m_FlySpeed = std::clamp(m_FlySpeed * l_Scale, 0.1f, 500.0f);
}

void EditorCamera::SetFlySpeed(float a_Speed)
{
    m_FlySpeed = std::clamp(a_Speed, 0.1f, 500.0f);
}

void EditorCamera::SyncToRuntimeCamera(const glm::vec3& a_Position, float a_YawDegrees, float a_PitchDegrees, float a_FieldOfViewDegrees)
{
    m_Position = a_Position;
    m_YawDegrees = a_YawDegrees;
    m_PitchDegrees = a_PitchDegrees;
    m_FieldOfViewDegrees = a_FieldOfViewDegrees;
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
}

void EditorCamera::ClampPitch()
{
    m_PitchDegrees = std::clamp(m_PitchDegrees, -89.0f, 89.0f);
}

void EditorCamera::UpdateCachedVectors()
{
    const float l_CosPitch = glm::cos(glm::radians(m_PitchDegrees));
    glm::vec3 l_Forward{};
    l_Forward.x = glm::cos(glm::radians(m_YawDegrees)) * l_CosPitch;
    l_Forward.y = glm::sin(glm::radians(m_YawDegrees)) * l_CosPitch;
    l_Forward.z = glm::sin(glm::radians(m_PitchDegrees));

    m_Forward = glm::normalize(l_Forward);
    m_Right = glm::normalize(glm::cross(m_Forward, s_WorldUp));
    if (glm::length(m_Right) < 0.0001f)
    {
        // When the forward vector aligns with world up we fallback to a canonical right.
        m_Right = glm::vec3{ 1.0f, 0.0f, 0.0f };
    }
    m_Up = glm::normalize(glm::cross(m_Right, m_Forward));
}