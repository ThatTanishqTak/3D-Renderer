#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

namespace Engine
{
	class Renderer
	{
	public:
		void Init();
		void Shutdown();

		void Draw();

	private:

	};
}