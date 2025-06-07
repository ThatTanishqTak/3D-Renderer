#include "Window/Window.h"

#include "Core/Utilities.h"

#include <stdexcept>

namespace Trident
{
    Window::Window(uint32_t width, uint32_t height, const std::string& title)
    {
        TR_CORE_INFO("-------INITIALIZING WINDOW-------");
        
        InitWindow(width, height, title);

        TR_CORE_INFO("-------WINDOW INITIALIZED-------");
    }

    Window::~Window()
    {
        Shutdown();
    }

    void Window::InitWindow(uint32_t width, uint32_t height, const std::string& title)
    {
        TR_CORE_TRACE("Creating GLFW Window");
        
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);

        if (!m_Window)
        {
            TR_CORE_ERROR("Failed to create GLFW window");
        }

        TR_CORE_TRACE("GLFW Window Created");
    }

    void Window::Shutdown()
    {
        TR_CORE_TRACE("Shutting Down Window");

        glfwDestroyWindow(m_Window);
        glfwTerminate();

        TR_CORE_TRACE("Window Shutdown Complete");
    }

    bool Window::ShouldClose() const
    {
        return glfwWindowShouldClose(m_Window);
    }

    void Window::PollEvents() const
    {
        glfwPollEvents();
    }

    void Window::GetFramebufferSize(uint32_t& width, uint32_t& height) const
    {
        int l_Width = 0;
        int l_Height = 0;
        
        glfwGetFramebufferSize(m_Window, &l_Width, &l_Height);
        width = static_cast<uint32_t>(l_Width);
        height = static_cast<uint32_t>(l_Height);
    }
}