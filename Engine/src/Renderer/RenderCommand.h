#pragma once

#include <raylib.h>

namespace Engine
{
	class RenderCommnad
	{
	public:
		void Init();
		void Shutdown();

		void Render();
		void RenderUI();
	};
}