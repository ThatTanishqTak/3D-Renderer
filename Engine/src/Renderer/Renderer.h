#pragma once

#include <raylib.h>

namespace Engine
{
	class Renderer
	{
	public:
		void Init();
		void Shutdown();

		void Update();

	public:
		Camera3D camera;
	};
}