#include "Application.h"

#include "Renderer/RenderCommand.h"
#include "Events/ApplicationEvents.h"

#include <stdexcept>

namespace Trident
{
    Application::Application()
    {
        Trident::Utilities::Log::Init();

        Inititialize();
    }

    Application::~Application()
    {
        // Ensure editor and UI resources tear down cleanly even if the host forgets to
        // call Shutdown explicitly.
        Shutdown();
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

        // Bootstrap the ImGui layer once the renderer is ready so editor widgets can
        // access the graphics context safely.
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

        // Share the ImGui layer with the renderer so it can route draw commands and
        // lifetime events appropriately.
        Startup::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());
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
        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->BeginFrame();
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
        if (m_HasShutdown)
        {
            return;
        }

        m_HasShutdown = true;

        // Tear down ImGui and detach it from the renderer so command buffers do not try
        // to access freed UI state.
        if (m_ImGuiLayer)
        {
            Startup::GetRenderer().SetImGuiLayer(nullptr);
            m_ImGuiLayer->Shutdown();
            m_ImGuiLayer.reset();
        }

        RenderCommand::Shutdown();

        // Release window and startup scaffolding last so Vulkan resources are already
        // flushed.
        m_Startup.reset();
        m_Window.reset();
    }
}