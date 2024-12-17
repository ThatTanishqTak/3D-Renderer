#include <raylib.h>

#include "Window.h"
#include "Specification.h"

namespace Engine
{
	Window::Window() : m_WindowWidth(specs.Width), m_WindowHeight(specs.Height), m_Title(specs.Title)
	{
		
	}

	void Window::Init()
	{
		SetConfigFlags(FLAG_WINDOW_RESIZABLE);
		InitWindow(m_WindowWidth, m_WindowHeight, m_Title.c_str());
		SetTargetFPS(60);
	}

	void Window::Shutdown()
	{
		CloseWindow();
	}
}