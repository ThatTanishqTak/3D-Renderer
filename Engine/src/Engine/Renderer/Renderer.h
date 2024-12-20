#pragma once

#include "RenderCommands.h"
#include "Engine/Interface/Interface.h"

namespace Engine
{
	class Renderer
	{
	public:
		void Init();
		void Shutdown();

		void Render();

	private:
		RenderCommands m_RenderCommands;
		Interface m_Interface;
	};
}