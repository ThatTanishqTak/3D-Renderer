#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include "Camera/CameraComponent.h"

namespace Trident
{
    class Camera
    {
    public:
        Camera() = default;
        explicit Camera(GLFWwindow* window);

        void Update(float deltaTime);
        glm::mat4 GetViewMatrix() const;

        // Camera transform accessors used by tooling panels.
        glm::vec3 GetPosition() const { return m_Position; }
        void SetPosition(const glm::vec3& position);

        float GetYaw() const { return m_Yaw; }
        void SetYaw(float yaw);

        float GetPitch() const { return m_Pitch; }
        void SetPitch(float pitch);

        // Projection parameter accessors for editor tweaking.
        float GetFOV() const { return m_FOV; }
        void SetFOV(float fovDegrees);

        float GetNearClip() const { return m_NearClip; }
        void SetNearClip(float nearClip);

        float GetFarClip() const { return m_FarClip; }
        void SetFarClip(float farClip);

        ProjectionType GetProjection() const { return m_Projection; }
        void SetProjection(ProjectionType projection);

        float GetOrthographicSize() const { return m_OrthographicSize; }
        void SetOrthographicSize(float size);

    private:
        void UpdateVectors();

        GLFWwindow* m_Window = nullptr;

        glm::vec3 m_Position{ 0.0f, -3.0f, 1.5f };
        float m_Yaw = 90.0f;    // Facing +Y
        float m_Pitch = -25.0f;

        glm::vec3 m_Front{ 0.0f, 1.0f, 0.0f };
        glm::vec3 m_Right{ 1.0f, 0.0f, 0.0f };
        glm::vec3 m_Up{ 0.0f, 0.0f, 1.0f };

        float m_FOV = 45.0f;
        float m_NearClip = 0.1f;
        float m_FarClip = 100.0f;
        ProjectionType m_Projection = ProjectionType::Perspective;
        float m_OrthographicSize = 10.0f;

        float m_MoveSpeed = 3.0f;
        float m_SpeedMultiplier = 5.0f;
        float m_MouseSensitivity = 0.05f;

        bool m_FirstMouse = true;
        double m_LastX = 0.0;
        double m_LastY = 0.0;
    };
}