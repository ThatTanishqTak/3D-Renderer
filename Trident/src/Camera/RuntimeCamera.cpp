#include "RuntimeCamera.h"

#include "Events/KeyCodes.h"
#include "Events/MouseCodes.h"

#include <GLFW/glfw3.h>

namespace Trident
{
    RuntimeCamera::RuntimeCamera(GLFWwindow* a_Window)
    {
        SetWindow(a_Window);
    }

    void RuntimeCamera::Update(float a_DeltaTime)
    {
        if (!m_Window)
        {
            // Nothing to update until the runtime provides a valid window handle.
            return;
        }

        HandleKeyboardInput(a_DeltaTime);
        HandleMouseInput();
    }

    void RuntimeCamera::SetWindow(GLFWwindow* a_Window)
    {
        // Allow the runtime to defer providing a window until the platform layer initialises.
        m_Window = a_Window;
        m_FirstMouse = true;

        if (!m_Window)
        {
            return;
        }

        double l_X = 0.0;
        double l_Y = 0.0;
        glfwGetCursorPos(m_Window, &l_X, &l_Y);
        m_LastX = l_X;
        m_LastY = l_Y;

        ClampPitch();
        UpdateCachedDirectionsFromAngles();
    }

    void RuntimeCamera::HandleKeyboardInput(float a_DeltaTime)
    {
        const bool l_RightMouseHeld = glfwGetMouseButton(m_Window, static_cast<int>(Mouse::ButtonRight)) == GLFW_PRESS;
        if (!l_RightMouseHeld)
        {
            // Match the editor behaviour where movement is only active while looking around.
            return;
        }

        float l_Speed = m_MoveSpeed;
        const bool l_LeftShiftHeld = glfwGetKey(m_Window, static_cast<int>(Key::LeftShift)) == GLFW_PRESS;
        const bool l_RightShiftHeld = glfwGetKey(m_Window, static_cast<int>(Key::RightShift)) == GLFW_PRESS;
        if (l_LeftShiftHeld || l_RightShiftHeld)
        {
            l_Speed *= m_SpeedMultiplier;
        }

        const float l_FrameMove = l_Speed * a_DeltaTime;

        if (glfwGetKey(m_Window, static_cast<int>(Key::W)) == GLFW_PRESS)
        {
            m_Position += m_Forward * l_FrameMove;
        }

        if (glfwGetKey(m_Window, static_cast<int>(Key::S)) == GLFW_PRESS)
        {
            m_Position -= m_Forward * l_FrameMove;
        }

        if (glfwGetKey(m_Window, static_cast<int>(Key::A)) == GLFW_PRESS)
        {
            m_Position -= m_Right * l_FrameMove;
        }

        if (glfwGetKey(m_Window, static_cast<int>(Key::D)) == GLFW_PRESS)
        {
            m_Position += m_Right * l_FrameMove;
        }

        if (glfwGetKey(m_Window, static_cast<int>(Key::Q)) == GLFW_PRESS)
        {
            m_Position += m_Up * l_FrameMove;
        }

        if (glfwGetKey(m_Window, static_cast<int>(Key::E)) == GLFW_PRESS)
        {
            m_Position -= m_Up * l_FrameMove;
        }
    }

    void RuntimeCamera::HandleMouseInput()
    {
        double l_X = 0.0;
        double l_Y = 0.0;
        glfwGetCursorPos(m_Window, &l_X, &l_Y);

        if (m_FirstMouse)
        {
            // Seed the delta history so the first update does not produce a large jump.
            m_LastX = l_X;
            m_LastY = l_Y;
            m_FirstMouse = false;
        }

        const bool l_RightMouseHeld = glfwGetMouseButton(m_Window, static_cast<int>(Mouse::ButtonRight)) == GLFW_PRESS;
        if (l_RightMouseHeld)
        {
            const float l_XOffset = static_cast<float>(m_LastX - l_X) * m_MouseSensitivity;
            const float l_YOffset = static_cast<float>(m_LastY - l_Y) * m_MouseSensitivity;

            m_YawDegrees += l_XOffset;
            m_PitchDegrees += l_YOffset;

            ClampPitch();
            UpdateCachedDirectionsFromAngles();
        }

        m_LastX = l_X;
        m_LastY = l_Y;
    }
}