#include "Application.h"

#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"


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
	while (m_Window.m_IsRunning)
	{
		m_Window.Update();

		m_Renderer.Update();
		m_Renderer.Render();
	}
}