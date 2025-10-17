#include "Camera.h"

#include "Core/Utilities.h"
#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Trident
{
    Camera::Camera(GLFWwindow* window) : m_Window(window)
    {
        double l_X = 0.0;
        double l_Y = 0.0;

        glfwGetCursorPos(m_Window, &l_X, &l_Y);
        m_LastX = l_X;
        m_LastY = l_Y;

        UpdateVectors();
    }

    void Camera::Update(float deltaTime)
    {
        if (!m_Window)
        {
            return;
        }

        // Poll using engine key codes so the camera is ready for the centralized input service.
        // TODO: Forward these lookups through the shared Input helper once it exists to keep editor/runtime parity.
        bool l_RightMouse = glfwGetMouseButton(m_Window, (Trident::Mouse::ButtonRight)) == GLFW_PRESS;

        if (l_RightMouse)
        {
            float l_Speed = m_MoveSpeed;
            if (glfwGetKey(m_Window, Trident::Key::LeftShift == GLFW_PRESS) || glfwGetKey(m_Window, Trident::Key::RightShift == GLFW_PRESS))
            {
                l_Speed *= m_SpeedMultiplier;
            }

            if (glfwGetKey(m_Window, Trident::Key::W) == GLFW_PRESS)
            {
                m_Position += m_Front * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, Trident::Key::S) == GLFW_PRESS)
            {
                m_Position -= m_Front * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, Trident::Key::A) == GLFW_PRESS)
            {
                m_Position -= m_Right * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, Trident::Key::D) == GLFW_PRESS)
            {
                m_Position += m_Right * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, Trident::Key::Q) == GLFW_PRESS)
            {
                m_Position += m_Up * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, Trident::Key::E) == GLFW_PRESS)
            {
                m_Position -= m_Up * l_Speed * deltaTime;
            }
        }

        double l_X = 0.0;
        double l_Y = 0.0;
        glfwGetCursorPos(m_Window, &l_X, &l_Y);

        if (m_FirstMouse)
        {
            m_LastX = l_X;
            m_LastY = l_Y;

            m_FirstMouse = false;
        }

        if (l_RightMouse)
        {
            float l_XOffset = static_cast<float>(m_LastX - l_X) * m_MouseSensitivity;
            float l_YOffset = static_cast<float>(m_LastY - l_Y) * m_MouseSensitivity;

            m_Yaw += l_XOffset;
            m_Pitch += l_YOffset;

            if (m_Pitch > 89.0f)
            {
                m_Pitch = 89.0f;
            }

            if (m_Pitch < -89.0f)
            {
                m_Pitch = -89.0f;
            }
        }

        m_LastX = l_X;
        m_LastY = l_Y;

        UpdateVectors();
    }

    void Camera::UpdateVectors()
    {
        glm::vec3 l_Front{};

        l_Front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
        l_Front.y = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
        l_Front.z = sin(glm::radians(m_Pitch));

        m_Front = glm::normalize(l_Front);
        m_Right = glm::normalize(glm::cross(m_Front, glm::vec3{ 0.0f, 0.0f, 1.0f }));
        m_Up = glm::normalize(glm::cross(m_Right, m_Front));
    }

    glm::mat4 Camera::GetViewMatrix() const
    {
        return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
    }

    void Camera::SetPosition(const glm::vec3& position)
    {
        // Allow editor tooling to translate the camera without depending on input events.
        m_Position = position;
    }

    void Camera::SetYaw(float yaw)
    {
        // Update the horizontal angle so editor tweaks immediately affect the view direction.
        m_Yaw = yaw;
        UpdateVectors();
    }

    void Camera::SetPitch(float pitch)
    {
        // Clamp vertical rotation to avoid flipping the camera upside down while editing.
        m_Pitch = std::clamp(pitch, -89.0f, 89.0f);
        UpdateVectors();
    }

    void Camera::SetFOV(float fovDegrees)
    {
        // Keep the field of view within a comfortable perspective range for editors.
        m_FOV = std::clamp(fovDegrees, 1.0f, 120.0f);
    }

    void Camera::SetNearClip(float nearClip)
    {
        // Prevent an invalid projection by ensuring the near plane stays in front of the camera.
        float l_ClampedNear = std::clamp(nearClip, 0.001f, m_FarClip - 0.001f);
        m_NearClip = l_ClampedNear;
    }

    void Camera::SetFarClip(float farClip)
    {
        // Keep the far plane sorted after the near plane while allowing deep draw distances.
        float l_MinFar = m_NearClip + 0.001f;
        m_FarClip = std::max(farClip, l_MinFar);
    }

    void Camera::SetProjection(ProjectionType a_Projection)
    {
        // Persist the preferred projection so editor widgets and the renderer resolve the same frustum type.
        m_Projection = a_Projection;
    }

    void Camera::SetOrthographicSize(float a_Size)
    {
        // Prevent a degenerate frustum by clamping the orthographic extent to a small positive value.
        const float l_MinimumSize = 0.001f;
        m_OrthographicSize = std::max(a_Size, l_MinimumSize);
    }
}