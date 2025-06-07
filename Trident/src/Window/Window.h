#pragma once

#include <GLFW/glfw3.h>

#include <string>
#include <cstdint>

namespace Trident
{
    class Window
    {
    public:
        Window(uint32_t width, uint32_t height, const std::string& title);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        bool ShouldClose() const;
        
        void PollEvents() const;
        void GetFramebufferSize(uint32_t& width, uint32_t& height) const;

        GLFWwindow* GetNativeWindow() const { return m_Window; }

    private:
        void InitWindow(uint32_t width, uint32_t height, const std::string& title);
        void Shutdown();

    private:
        GLFWwindow* m_Window = nullptr;
    };
}