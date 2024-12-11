#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Core/InputManager.h"
#include "Engine/Renderer/Renderer.h"

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
	Engine::InputManager m_InputManager;
	Engine::Renderer m_Renderer;
};