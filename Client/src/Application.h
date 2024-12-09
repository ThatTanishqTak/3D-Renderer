#pragma once

#include "Window.h"

class Application
{
public:
	Application();
	~Application();

	void Init();
	void Shutdown();

	void Run();

private:
	Engine::Window m_Window;

};