#include "Window.h"

namespace Engine
{
	void Window::Init()
	{
		m_WindowWidth = m_WindowSpecs.Width;
		m_WindowHeight = m_WindowSpecs.Height;

		m_IsRunning = true;
		m_Title = m_WindowSpecs.Title;
		m_Icon = m_WindowSpecs.Icon;

		SetConfigFlags(FLAG_WINDOW_RESIZABLE);
		SetWindowIcon(m_Icon);
		InitWindow(m_WindowWidth, m_WindowHeight, m_Title.c_str());
		SetTargetFPS(60);
	}

	void Window::Shutdown()
	{
		CloseWindow();
	}

	void Window::Update()
	{
		m_IsRunning = !WindowShouldClose();
	}
}