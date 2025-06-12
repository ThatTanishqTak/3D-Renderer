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
        m_Engine->RenderScene();
        
        RenderUI();
    }
}

void ApplicationLayer::RenderUI()
{
    Trident::Application::GetImGuiLayer().Begin();
    Trident::Application::GetImGuiLayer().SetupDockspace();

    // --- Viewport window ---
    ImGui::Begin("Viewport");
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (viewportSize.x > 0.0f && viewportSize.y > 0.0f)
    {
        m_Viewport.Position = { viewportPos.x, viewportPos.y };
        m_Viewport.Size = { viewportSize.x, viewportSize.y };

        m_Engine->GetRenderer().SetViewport(m_Viewport);
    }
    ImGui::End();

    // --- Properties panel ---
    ImGui::Begin("Properties");
    ImGui::DragFloat3("Position", glm::value_ptr(m_CubeProps.Position), 0.1f);
    ImGui::DragFloat3("Rotation", glm::value_ptr(m_CubeProps.Rotation), 0.1f);
    ImGui::DragFloat3("Scale", glm::value_ptr(m_CubeProps.Scale), 0.1f);
    ImGui::End();

    // --- Update viewport info to pass to renderer ---
    m_Viewport.Position = { viewportPos.x, viewportPos.y };
    m_Viewport.Size = { viewportSize.x, viewportSize.y };

    m_Engine->GetRenderer().SetViewport(m_Viewport);
    m_Engine->GetRenderer().SetCubeProperties(m_CubeProps);

    Trident::Application::GetImGuiLayer().End(m_Engine->GetRenderer().GetCommandBuffer().at(Trident::Application::GetRenderer().GetCurrentFrame()));
}