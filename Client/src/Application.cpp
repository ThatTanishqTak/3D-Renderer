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
}

void Application::Shutdown()
{
	m_Window.Shutdown();
}

void Application::Run()
{
	m_Window.Run();
}
