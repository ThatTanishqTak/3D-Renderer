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
	// Initialization
	m_Window.Init();
	m_Renderer.Init();
}

void Application::Shutdown()
{
	// Clean-Up
	m_Renderer.Shutdown();
	m_Window.Shutdown();
}

void Application::Run()
{
	while (!WindowShouldClose())
	{
		// Render
		m_Renderer.Render();
	}
}