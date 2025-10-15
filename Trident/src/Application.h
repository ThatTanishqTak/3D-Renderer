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

namespace Trident
{
    class Application
    {
    public:
        Application();

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
        bool m_IsRunning = true;
    };
}