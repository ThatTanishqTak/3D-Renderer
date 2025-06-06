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
        TR_CORE_INFO("Creating GLFW Window");
        
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_Window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);

        if (!m_Window)
        {
            TR_CORE_ERROR("Failed to create GLFW window");
        }

        TR_CORE_INFO("GLFW Window Created");
    }

    void Window::Shutdown()
    {
        TR_CORE_INFO("Shutting Down Window");

        glfwDestroyWindow(m_Window);
        glfwTerminate();

        TR_CORE_INFO("Window Shutdown");
    }

    bool Window::ShouldClose() const
    {
        return glfwWindowShouldClose(m_Window);
    }

    void Window::PollEvents() const
    {
        glfwPollEvents();
    }
}