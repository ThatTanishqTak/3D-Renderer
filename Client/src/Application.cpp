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
	m_Renderer.Init();
}

void Application::Shutdown()
{
	m_Window.Shutdown();
	m_Renderer.Shutdown();
}

void Application::Run()
{
	while (!WindowShouldClose())
	{
		// Update


		// Render
		m_Renderer.Update();
	}
}