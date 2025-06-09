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
        m_Engine->RenderUI();
    }
}

void ApplicationLayer::RenderUI()
{

}