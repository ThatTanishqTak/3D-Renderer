#pragma once

#include "RenderCommands.h"

//#include <raylib.h>

namespace Engine
{
	class Renderer
	{
	public:
		void Draw();
		//Camera3D GetCamera();

	private:
		RenderCommands m_RenderCommands;
	};
}