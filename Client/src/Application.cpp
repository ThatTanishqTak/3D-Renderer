#include "Application.h"

#include "raylib.h"

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
	while (!WindowShouldClose())
	{
		ClearBackground(BLACK);
		BeginDrawing();

		EndDrawing();
	}
}