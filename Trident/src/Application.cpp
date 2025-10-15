#include "Application.h"

#include "Renderer/RenderCommand.h"
#include "Events/ApplicationEvents.h"

namespace Trident
{
    Application::Application()
    {
        Trident::Utilities::Log::Init();

        Inititialize();
    }

    void Application::Inititialize()
    {
        m_Specifications.Width = 1920;
        m_Specifications.Height = 1080;
        m_Specifications.Title = "Trident-Forge";

        m_Window = std::make_unique<Window>(m_Specifications);
        m_Window->SetEventCallback([this](Events& l_Event)
            {
                // Route every GLFW callback through the Application entry point so systems can react.
                OnEvent(l_Event);
            });
        m_Startup = std::make_unique<Startup>(*m_Window);

        RenderCommand::Init();
    }

    void Application::Run()
    {
        while (m_IsRunning && !m_Window->ShouldClose())
        {
            Update();

            Render();
        }
    }

    void Application::Update()
    {
        Utilities::Time::Update();

        m_Window->PollEvents();
    }

    void Application::Render()
    {
        RenderCommand::DrawFrame();
    }

    void Application::OnEvent(Events& event)
    {
        // Dispatch events by type so only the relevant handler executes and other listeners remain extendable.
        EventDispatcher l_Dispatcher(event);

        l_Dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& l_Event)
            {
                (void)l_Event;

                m_IsRunning = false;

                return true;
            });

        // Future event types (input, window focus, etc.) can be dispatched here without modifying the callback wiring.
    }

    void Application::Shutdown()
    {
        RenderCommand::Shutdown();
    }
}