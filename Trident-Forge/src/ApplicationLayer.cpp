#include "ApplicationLayer.h"

#include <imgui.h>

ApplicationLayer::ApplicationLayer()
{
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    m_Engine->Init();
}

ApplicationLayer::~ApplicationLayer()
{
    TR_INFO("-------SHUTING DOWN APPLICATION-------");

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
        RenderUI();
        m_Engine->RenderScene();
    }
}

void ApplicationLayer::RenderUI()
{
    Trident::Application::GetImGuiLayer().Begin();
    Trident::Application::GetImGuiLayer().SetupDockspace();

    ImVec2 viewportPos{ 0.0f, 0.0f };
    ImVec2 viewportSize{ 0.0f, 0.0f };

    if (ImGui::Begin("Scene"))
    {
        viewportPos = ImGui::GetCursorScreenPos();
        viewportSize = ImGui::GetContentRegionAvail();
        ImGui::Dummy(viewportSize);
    }
    ImGui::End();

    if (ImGui::Begin("Properties"))
    {
        ImGui::DragFloat3("Position", glm::value_ptr(m_CubeProps.Position), 0.1f);
        ImGui::DragFloat3("Rotation", glm::value_ptr(m_CubeProps.Rotation), 0.1f);
        ImGui::DragFloat3("Scale", glm::value_ptr(m_CubeProps.Scale), 0.1f);
    }
    ImGui::End();

    m_Viewport.Position = { viewportPos.x, viewportPos.y };
    m_Viewport.Size = { viewportSize.x, viewportSize.y };
    Trident::Application::GetRenderer().SetViewport(m_Viewport);
    Trident::Application::GetRenderer().SetCubeProperties(m_CubeProps);
}