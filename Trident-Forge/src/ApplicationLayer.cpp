#include "ApplicationLayer.h"

#include <imgui.h>

ApplicationLayer::ApplicationLayer()
{
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    m_Engine->Init();

    m_ImGuiLayer = std::make_unique<Trident::UI::ImGuiLayer>();
    m_ImGuiLayer->Init(m_Window->GetNativeWindow(),
        Trident::Application::GetInstance(),
        Trident::Application::GetPhysicalDevice(),
        Trident::Application::GetDevice(),
        Trident::Application::GetQueueFamilyIndices().GraphicsFamily.value(),
        Trident::Application::GetGraphicsQueue(),
        Trident::Application::GetRenderer().GetRenderPass(),
        Trident::Application::GetRenderer().GetImageCount(),
        Trident::Application::GetRenderer().GetCommandPool());
    Trident::Application::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());
}

ApplicationLayer::~ApplicationLayer()
{
    TR_INFO("-------SHUTING DOWN APPLICATION-------");

    if (m_ImGuiLayer)
    {
        m_ImGuiLayer->Shutdown();
    }

    if (m_Engine)
    {
        m_Engine->Shutdown();
    }

    TR_INFO("-------APPLICATION SHUTDOWN-------");
}

void ApplicationLayer::Run()
{
    while (!m_Window->ShouldClose())
    {
        m_Engine->Update();

        m_ImGuiLayer->BeginFrame();
        ImGui::Begin("Stats");
        ImGui::Text("FPS: %.2f", Trident::Utilities::Time::GetFPS());
        ImGui::End();
        m_ImGuiLayer->EndFrame();

        m_Engine->RenderScene();
    }
}