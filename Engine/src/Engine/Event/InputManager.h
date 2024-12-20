#pragma once

#include "Engine/Application/Window.h"
#include "Engine/Renderer/Renderer.h"


namespace Engine
{
	class InputManager
	{
	public:
		void Update();

	private:
		Window m_Window;
		Renderer m_Renderer;
	};
}