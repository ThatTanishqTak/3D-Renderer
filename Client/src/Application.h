#pragma once

#include "Window.h"
#include "Renderer.h"
#include "Camera.h"

#include <raylib.h>

class Application
{
public:
	Application();
	~Application();

	void Run();
	
private:
	void Init();
	void Shutdown();

private:
	Engine::Window m_Window;
	Engine::Renderer m_Renderer;
	Engine::Camera m_Camera;
};