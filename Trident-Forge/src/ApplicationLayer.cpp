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
}

void ApplicationLayer::Render()
{
	m_ViewportPanel.Render();
}