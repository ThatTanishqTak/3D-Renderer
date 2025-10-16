#include "ApplicationLayer.h"

void ApplicationLayer::Initialize()
{

}

void ApplicationLayer::Shutdown()
{

}

void ApplicationLayer::Update()
{
    m_ViewportPanel.Update();
    m_ContentBrowserPanel.Update();
    m_SceneHierarchyPanel.Update();
}

void ApplicationLayer::Render()
{
    m_ViewportPanel.Render();
    m_ContentBrowserPanel.Render();
    m_SceneHierarchyPanel.Render();
}