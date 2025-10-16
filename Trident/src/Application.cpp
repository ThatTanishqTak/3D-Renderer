#include "Application.h"

#include "Renderer/RenderCommand.h"
#include "Events/ApplicationEvents.h"

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

        RenderCommand::Init();

        // Bootstrap the ImGui layer once the renderer is ready so editor widgets can access the graphics context safely.
        m_ImGuiLayer = std::make_unique<UI::ImGuiLayer>();

        const QueueFamilyIndices l_QueueFamilyIndices = Startup::GetQueueFamilyIndices();
        if (!l_QueueFamilyIndices.GraphicsFamily.has_value() || !l_QueueFamilyIndices.PresentFamily.has_value())
        {
            throw std::runtime_error("Queue family indices are not initialised before ImGui setup.");
        }

        const VkQueue l_GraphicsQueue = Startup::Get().GetGraphicsQueue();
        const VkQueue l_PresentQueue = Startup::Get().GetPresentQueue();
        (void)l_PresentQueue; // Present queue reserved for future multi-queue UI work.

        m_ImGuiLayer->Init(m_Window->GetNativeWindow(), Startup::GetInstance(), Startup::GetPhysicalDevice(), Startup::GetDevice(), l_QueueFamilyIndices.GraphicsFamily.value(),
            l_GraphicsQueue, Startup::GetRenderer().GetRenderPass(), static_cast<uint32_t>(Startup::GetRenderer().GetImageCount()), Startup::GetRenderer().GetCommandPool());

        // Share the ImGui layer with the renderer so it can route draw commands and lifetime events appropriately.
        Startup::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());

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

        m_Window->PollEvents();

        // Update the active layer after input/events so it can react to the latest state.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Update();
        }
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

        RenderCommand::DrawFrame();
    }

    void Application::OnEvent(Events& event)
    {
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
            Startup::GetRenderer().SetImGuiLayer(nullptr);
            m_ImGuiLayer->Shutdown();
            m_ImGuiLayer.reset();
        }

        RenderCommand::Shutdown();

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