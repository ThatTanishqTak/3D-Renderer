#pragma once

#include <glm/glm.hpp>

#include "Camera/CameraComponent.h"

namespace Trident
{
    /**
     * Lightweight base interface describing the minimal surface area required by the renderer.
     * Similar to the Layer class, this type lets us author multiple camera controllers while
     * keeping the core system unaware of editor specific dependencies such as GLFW.
     */
    class Camera
    {
    public:
        virtual ~Camera() = default;

        /**
         * Called once per frame so derived cameras can update their internal state.
         */
        virtual void Update(float a_DeltaTime) = 0;

        /**
         * Returns the view matrix representing the camera's transform in world space.
         */
        virtual glm::mat4 GetViewMatrix() const;

        /**
         * Camera transform accessors that tooling panels rely on when presenting gizmos.
         */
        glm::vec3 GetPosition() const { return m_Position; }
        virtual void SetPosition(const glm::vec3& a_Position);

        float GetYaw() const { return m_YawDegrees; }
        virtual void SetYaw(float a_YawDegrees);

        float GetPitch() const { return m_PitchDegrees; }
        virtual void SetPitch(float a_PitchDegrees);

        /**
         * Projection parameter accessors exposed to the editor for tweaking.
         */
        float GetFOV() const { return m_FieldOfViewDegrees; }
        virtual void SetFOV(float a_FieldOfViewDegrees);

        float GetNearClip() const { return m_NearClip; }
        virtual void SetNearClip(float a_NearClip);

        float GetFarClip() const { return m_FarClip; }
        virtual void SetFarClip(float a_FarClip);

        ProjectionType GetProjection() const { return m_Projection; }
        virtual void SetProjection(ProjectionType a_Projection);

        float GetOrthographicSize() const { return m_OrthographicSize; }
        virtual void SetOrthographicSize(float a_Size);

    protected:
        Camera() = default;

        /**
         * Helper used by derived classes to rebuild direction vectors after yaw/pitch changes.
         */
        void UpdateCachedDirectionsFromAngles();

        /**
         * Clamps the pitch value to a reasonable range to prevent gimbal lock.
         */
        void ClampPitch();

        glm::vec3 m_Position{ 0.0f, -3.0f, 1.5f };
        float m_YawDegrees = 90.0f;    // Facing +Y
        float m_PitchDegrees = -25.0f;

        glm::vec3 m_Forward{ 0.0f, 1.0f, 0.0f };
        glm::vec3 m_Right{ 1.0f, 0.0f, 0.0f };
        glm::vec3 m_Up{ 0.0f, 0.0f, 1.0f };

        float m_FieldOfViewDegrees = 45.0f;
        float m_NearClip = 0.1f;
        float m_FarClip = 100.0f;
        ProjectionType m_Projection = ProjectionType::Perspective;
        float m_OrthographicSize = 10.0f;
    };
}