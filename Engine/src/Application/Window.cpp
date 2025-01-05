#include "Window.h"

namespace Engine
{
	Window::Window() : m_IsRunning(true), m_Title(m_ApplicationSpecs.Title), m_Icon(m_ApplicationSpecs.Icon)
	{
		m_WindowWidth = m_ApplicationSpecs.Width;
		m_WindowHeight = m_ApplicationSpecs.Height;
	}

	void Window::Init()
	{
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