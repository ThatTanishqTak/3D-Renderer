#pragma once

#include <raylib.h>

#include "RenderCommand.h"
#include "../Camera/Camera.h"

namespace Engine
{
	class Renderer
	{
	public:
		void Init();
		void Shutdown();

		void Update();

	public:
		Camera m_Camera;
		RenderCommnad m_RenderCommand;
	};
}