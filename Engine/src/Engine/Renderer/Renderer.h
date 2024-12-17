#pragma once

#include "RenderCommands.h"
#include "../GUI/UserInterface.h"

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
		UserInterface m_UserInterface;
	};
}