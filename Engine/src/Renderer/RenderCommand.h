#pragma once

#include <raylib.h>

#include "../Camera/Camera.h"

namespace Engine
{
	class RenderCommnad
	{
	public:
		void Init();
		void Shutdown();

		void RenderGrid();
		void RenderUI();
		void RenderScene();

	private:
		Camera m_Camera;
	};
}