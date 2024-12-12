#include "Application.h"

Application::Application()
{
	Init();
}

Application::~Application()
{
	Shutdown();
}

void Application::Init()
{
	m_Window.Init();
	m_Running = m_Window.m_Running;
}

void Application::Shutdown()
{
	m_Window.Shutdown();
}

void Application::Run()
{
	while (m_Running)
	{
		m_Renderer.Draw();
	}
}