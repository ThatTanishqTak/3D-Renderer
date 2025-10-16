#pragma once

#include <memory>
#include <string>

struct ApplicationSpecifications
{
    int Width = 1920;
    int Height = 1080;
    std::string Title = "Trident-Application";
};

#include "Window/Window.h"
#include "Application/Startup.h"
#include "Events/Events.h"
#include "UI/ImGuiLayer.h"

namespace Trident
{
    class Application
    {
    public:
        Application();
        ~Application();

        void Inititialize();
        void Shutdown();

        void Run();
        void OnEvent(Events& event);

    private:
        void Update();
        void Render();

    private:
        ApplicationSpecifications m_Specifications;
        std::unique_ptr<Startup> m_Startup;
        std::unique_ptr<Window> m_Window;
        std::unique_ptr<UI::ImGuiLayer> m_ImGuiLayer;
        bool m_HasShutdown = false;
        bool m_IsRunning = true;
    };
}