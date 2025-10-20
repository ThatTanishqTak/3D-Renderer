#include "Window/Window.h"

#include "Application.h"
#include "Application/Input.h"
#include "Core/Utilities.h"

#include "Events/ApplicationEvents.h"
#include "Events/KeyEvents.h"
#include "Events/MouseEvents.h"

#include <vector>

namespace Trident
{
    Window::Window(const ApplicationSpecifications& specs)
    {
        TR_CORE_INFO("-------INITIALIZING WINDOW-------");
        
        m_Data.m_Width = static_cast<uint32_t>(specs.Width);
        m_Data.m_Height = static_cast<uint32_t>(specs.Height);
        m_Data.m_Title = specs.Title;

        InitWindow(m_Data.m_Width, m_Data.m_Height, m_Data.m_Title);

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
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);

        if (!m_Window)
        {
            TR_CORE_ERROR("Failed to create GLFW window");

            return;
        }

        // Store the engine side data within GLFW so the callbacks can forward events back to the application layer.
        glfwSetWindowUserPointer(m_Window, &m_Data);

        // Forward GLFW callbacks to Trident's strongly typed event system.
        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
                l_Data.m_Width = static_cast<uint32_t>(width);
                l_Data.m_Height = static_cast<uint32_t>(height);

                WindowResizeEvent l_Event(static_cast<unsigned int>(width), static_cast<unsigned int>(height));

                if (l_Data.m_EventCallback)
                {
                    l_Data.m_EventCallback(l_Event);
                }
            });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

                WindowCloseEvent l_Event;

                if (l_Data.m_EventCallback)
                {
                    l_Data.m_EventCallback(l_Event);
                }
            });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

                (void)scancode;
                (void)mods;

                switch (action)
                {
                case GLFW_PRESS:
                {
                    Trident::Input::Get().OnKeyPressed(static_cast<KeyCode>(key), false);
                    KeyPressedEvent l_Event(static_cast<KeyCode>(key), false);

                    if (l_Data.m_EventCallback)
                    {
                        l_Data.m_EventCallback(l_Event);
                    }

                    break;
                }
                case GLFW_RELEASE:
                {
                    Trident::Input::Get().OnKeyReleased(static_cast<KeyCode>(key));
                    KeyReleasedEvent l_Event(static_cast<KeyCode>(key));

                    if (l_Data.m_EventCallback)
                    {
                        l_Data.m_EventCallback(l_Event);
                    }

                    break;
                }
                case GLFW_REPEAT:
                {
                    Trident::Input::Get().OnKeyPressed(static_cast<KeyCode>(key), true);
                    KeyPressedEvent l_Event(static_cast<KeyCode>(key), true);

                    if (l_Data.m_EventCallback)
                    {
                        l_Data.m_EventCallback(l_Event);
                    }

                    break;
                }
                default:
                    break;
                }
            });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

                (void)mods;

                switch (action)
                {
                case GLFW_PRESS:
                {
                    Trident::Input::Get().OnMouseButtonPressed(static_cast<MouseCode>(button));
                    MouseButtonPressedEvent l_Event(static_cast<MouseCode>(button));

                    if (l_Data.m_EventCallback)
                    {
                        l_Data.m_EventCallback(l_Event);
                    }

                    break;
                }
                case GLFW_RELEASE:
                {
                    Trident::Input::Get().OnMouseButtonReleased(static_cast<MouseCode>(button));
                    MouseButtonReleasedEvent l_Event(static_cast<MouseCode>(button));

                    if (l_Data.m_EventCallback)
                    {
                        l_Data.m_EventCallback(l_Event);
                    }

                    break;
                }
                default:
                    break;
                }
            });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

                MouseMovedEvent l_Event(static_cast<float>(xPos), static_cast<float>(yPos));

                if (l_Data.m_EventCallback)
                {
                    l_Data.m_EventCallback(l_Event);
                }
            });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

                MouseScrolledEvent l_Event(static_cast<float>(xOffset), static_cast<float>(yOffset));

                if (l_Data.m_EventCallback)
                {
                    l_Data.m_EventCallback(l_Event);
                }
            });

        glfwSetDropCallback(m_Window, [](GLFWwindow* window, int pathCount, const char** paths)
            {
                WindowData& l_Data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

                std::vector<std::string> l_NormalizedPaths{};
                l_NormalizedPaths.reserve(static_cast<size_t>(pathCount));

                for (int it_Path = 0; it_Path < pathCount; ++it_Path)
                {
                    const char* l_RawPath = (paths && paths[it_Path]) ? paths[it_Path] : nullptr;
                    if (!l_RawPath)
                    {
                        continue;
                    }

                    std::string l_Normalized = Utilities::FileManagement::NormalizePath(l_RawPath);
                    if (!l_Normalized.empty())
                    {
                        l_NormalizedPaths.push_back(std::move(l_Normalized));
                    }
                }

                if (l_NormalizedPaths.empty())
                {
                    return;
                }

                FileDropEvent l_Event(std::move(l_NormalizedPaths));

                if (l_Data.m_EventCallback)
                {
                    l_Data.m_EventCallback(l_Event);
                }
            });

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

    void Window::SetEventCallback(const std::function<void(Events&)>& callback)
    {
        m_Data.m_EventCallback = callback;
    }
}