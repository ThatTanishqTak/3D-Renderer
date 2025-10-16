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
#include "Layer/Layer.h"
#include "Events/Events.h"
#include "UI/ImGuiLayer.h"

namespace Trident
{
    class Application
    {
    public:
        Application();
        explicit Application(std::unique_ptr<Layer> layer);
        ~Application();

        void Inititialize();
        void Shutdown();

        void Run();
        void OnEvent(Events& event);

        /**
         * Allows hosts to swap in their own layer before Run() executes, keeping the core engine agnostic of gameplay code.
         */
        void SetActiveLayer(std::unique_ptr<Layer> layer);

    private:
        void Update();
        void Render();

    private:
        ApplicationSpecifications m_Specifications;
        std::unique_ptr<Startup> m_Startup;
        std::unique_ptr<Window> m_Window;
        std::unique_ptr<UI::ImGuiLayer> m_ImGuiLayer;
        std::unique_ptr<Layer> m_ActiveLayer;
        bool m_HasShutdown = false;
        bool m_IsRunning = true;
    };
}