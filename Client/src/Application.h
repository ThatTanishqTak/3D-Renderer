#pragma once

#include "Window.h"
#include "Renderer.h"

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
};