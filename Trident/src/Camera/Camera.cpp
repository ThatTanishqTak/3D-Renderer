#include "Camera.h"

#include "Core/Utilities.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Trident
{
    Camera::Camera(GLFWwindow* window) : m_Window(window)
    {
        double l_X = 0.0;
        double l_Y = 0.0;

        glfwGetCursorPos(m_Window, &l_X, &l_Y);
        m_LastX = l_X;
        m_LastY = l_Y;
    }

    void Camera::Update(float deltaTime)
    {
        if (!m_Window)
        {
            return;
        }

        bool l_RightMouse = glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if (l_RightMouse)
        {
            float l_Speed = m_MoveSpeed;
            if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
            {
                l_Speed *= m_SpeedMultiplier;
            }

            if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS)
            {
                m_Position += m_Front * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS)
            {
                m_Position -= m_Front * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS)
            {
                m_Position -= m_Right * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS)
            {
                m_Position += m_Right * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, GLFW_KEY_Q) == GLFW_PRESS)
            {
                m_Position += m_Up * l_Speed * deltaTime;
            }

            if (glfwGetKey(m_Window, GLFW_KEY_E) == GLFW_PRESS)
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
}