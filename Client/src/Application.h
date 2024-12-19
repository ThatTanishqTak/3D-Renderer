#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Event/InputManager.h"
#include "Engine/Renderer/Renderer.h"
#include "Engine/Renderer/RenderCommands.h"

#include <raylib/include/raylib.h>

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
	Engine::RenderCommands m_RenderCommands;
};