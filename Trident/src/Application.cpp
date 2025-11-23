#include "Application.h"

#include "Events/ApplicationEvents.h"
#include "Application/Input.h"

#include <utility>
#include <stdexcept>

namespace Trident
{
    Application::Application() : Application(nullptr)
    {

    }

    Application::Application(std::unique_ptr<Layer> layer) : m_ActiveLayer(std::move(layer))
    {
        Trident::Utilities::Log::Init();
        Trident::Utilities::Time::Init();

        Inititialize();
    }

    Application::~Application()
    {
        // Ensure editor and UI resources tear down cleanly even if the host forgets to call Shutdown explicitly.
        Shutdown();
    }

    void Application::Inititialize()
    {
        m_Specifications.Width = 1920;
        m_Specifications.Height = 1080;
        m_Specifications.Title = "Trident-Forge";

        m_Window = std::make_unique<Window>(m_Specifications);
        m_Window->SetEventCallback([this](Events& event)
            {
                // Route every GLFW callback through the Application entry point so systems can react.
                OnEvent(event);
            });
        m_Startup = std::make_unique<Startup>(*m_Window);

        // Bootstrap the ImGui layer once the renderer is ready so editor widgets can access the graphics context safely.
        m_ImGuiLayer = std::make_unique<UI::ImGuiLayer>();

        // Fully initialises ImGui so renderer-side texture registration calls do not trip assertions about missing contexts.
        m_ImGuiLayer->Initialize();

        // Once the renderer is configured, the active layer can allocate gameplay/editor resources safely.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Initialize();
        }
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

        Input::Get().BeginFrame();

        m_Window->PollEvents();

        // Update the active layer after input/events so it can react to the latest state.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Update();
        }
        // Reset one-shot input edges so the next tick starts with a clean slate while
        // keeping the held state active. Future controller or text helpers can share this hook.
        Input::Get().EndFrame();
    }

    void Application::Render()
    {
        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->BeginFrame();
        }

        // Allow the gameplay/editor layer to submit draw data before the UI finalises the frame.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Render();
        }

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->EndFrame();
        }

        // After the main swapchain submission, render any detached ImGui platform windows so they remain visible.
        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->RenderAdditionalViewports();
        }
    }

    void Application::OnEvent(Events& event)
    {
        const std::string l_EventDescription = event.ToString();
        //TR_CORE_TRACE("Received event: {}", l_EventDescription);

        // Dispatch events by type so only the relevant handler executes and other listeners remain extendable.
        EventDispatcher l_Dispatcher(event);

        l_Dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event)
            {
                (void)event;

                m_IsRunning = false;

                return true;
            });

        if (m_ActiveLayer)
        {
            // Forward the event to the active layer so editor tooling and gameplay can react to callbacks such as file drops.
            m_ActiveLayer->OnEvent(event);
        }

        // Future event types (input, window focus, etc.) can be dispatched here without modifying the callback wiring.
    }

    void Application::Shutdown()
    {
        TR_CORE_INFO("-------SHUTTING DOWN APPLICATION-------");

        if (m_HasShutdown)
        {
            return;
        }

        m_HasShutdown = true;

        vkDeviceWaitIdle(Startup::GetDevice());

        // Ask the active layer to release its resources while the renderer context is still valid.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Shutdown();
            m_ActiveLayer.reset();
        }

        // Tear down ImGui and detach it from the renderer so command buffers do not try
        // to access freed UI state.
        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->Shutdown();
            m_ImGuiLayer.reset();
        }


        // Release window and startup scaffolding last so Vulkan resources are already flushed.
        m_Startup.reset();
        m_Window.reset();

        TR_CORE_INFO("-------APPLICATION SHUTDOWN COMPLETE-------");
    }

    void Application::SetActiveLayer(std::unique_ptr<Layer> layer)
    {
        // Ensure any previous layer unwinds before we replace it to avoid dangling GPU handles.
        if (m_ActiveLayer && m_Startup)
        {
            m_ActiveLayer->Shutdown();
        }

        m_ActiveLayer = std::move(layer);

        // If the engine is already initialised boot the new layer immediately.
        if (m_ActiveLayer && m_Startup)
        {
            m_ActiveLayer->Initialize();
        }
    }
}