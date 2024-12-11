#pragma once

#include "RenderCommands.h"

namespace Engine
{
	class Renderer
	{
	public:
		void Draw();

	private:
		RenderCommands render;
	};
}