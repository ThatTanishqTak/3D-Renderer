#pragma once

#include <memory>
#include <string>

struct ApplicationSpecifications
{
    int Width = 1920;
    int Height = 1080;
    std::string Title = "Trident-Application";
} specifications;

#include "Window/Window.h"
#include "Application/Startup.h"
#include "Renderer/Renderer.h"

namespace Trident
{
    class Application
    {
    public:
        Application();

        void Inititialize();
        void Shutdown();

        void Run();

    private:
        void Update();
        void Render();

    private:
        std::unique_ptr<Startup> m_Startup;
        std::unique_ptr<Window> m_Window;
        std::unique_ptr<Renderer> m_Renderer;
    };
}