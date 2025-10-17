#pragma once

#include "Camera/Camera.h"

#include <GLFW/glfw3.h>

namespace Trident
{
    /**
     * Runtime camera controller that handles standard WASD navigation with mouse look.
     * The class derives from the lightweight Camera base so the renderer can talk to it
     * polymorphically while the editor provides its own specialised implementation.
     */
    class RuntimeCamera final : public Camera
    {
    public:
        explicit RuntimeCamera(GLFWwindow* a_Window);

        void Update(float a_DeltaTime) override;

        void SetWindow(GLFWwindow* a_Window);

    private:
        void HandleKeyboardInput(float a_DeltaTime);
        void HandleMouseInput();

        GLFWwindow* m_Window = nullptr;

        float m_MoveSpeed = 3.0f;
        float m_SpeedMultiplier = 5.0f;
        float m_MouseSensitivity = 0.05f;

        bool m_FirstMouse = true;
        double m_LastX = 0.0;
        double m_LastY = 0.0;
    };
}