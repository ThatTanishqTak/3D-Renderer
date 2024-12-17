#pragma once

#include "RenderCommands.h"
#include "../GUI/GUI.h"

namespace Engine
{
	class Renderer
	{
	public:
		void Init();
		void Shutdown();

		void Update();

	private:
		RenderCommands m_RenderCommands;
		GUI m_Gui;
	};
}