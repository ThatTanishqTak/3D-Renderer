#include "ApplicationLayer.h"

ApplicationLayer::ApplicationLayer()
{
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    m_Engine->Init();
}

ApplicationLayer::~ApplicationLayer()
{
    if (m_Engine)
    {
        m_Engine->Shutdown();
    }
}

void ApplicationLayer::Run()
{
    while (!m_Window->ShouldClose())
    {
        m_Engine->Update();
    }
}