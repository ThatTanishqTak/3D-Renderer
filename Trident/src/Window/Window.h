#pragma once

#include <GLFW/glfw3.h>

#include <string>
#include <cstdint>
#include <functional>

struct ApplicationSpecifications;

namespace Trident
{
    class Events;

    class Window
    {
    public:
        Window(const ApplicationSpecifications& specs);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        bool ShouldClose() const;

        void PollEvents() const;
        void GetFramebufferSize(uint32_t& width, uint32_t& height) const;

        GLFWwindow* GetNativeWindow() const { return m_Window; }

        /**
         * @brief Registers a callback that propagates GLFW events as Trident events to the engine.
         */
        void SetEventCallback(const std::function<void(Events&)>& callback);

    private:
        void InitWindow(uint32_t width, uint32_t height, const std::string& title);
        void Shutdown();

    private:
        /**
         * @brief Internal data that mirrors GLFW window state so the engine can query metadata and fire events.
         */
        struct WindowData
        {
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            std::string m_Title;
            std::function<void(Events&)> m_EventCallback;
        };

        GLFWwindow* m_Window = nullptr;
        WindowData m_Data;
    };
}