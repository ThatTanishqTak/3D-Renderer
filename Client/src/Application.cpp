#include "Application.h"

#include <raylib/include/raylib.h>

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
	m_Renderer.Shutdown();
	m_Window.Shutdown();
}

void Application::Run()
{
	while (!WindowShouldClose())
	{
		m_InputManager.Update();
		m_Renderer.Update();
	}
}