#pragma once

#include <string>

namespace Engine
{
	struct WindowSpecification
	{
		int Width = 1920;
		int Height = 1080;
		std::string Title = "3D Renderer";
	};

	WindowSpecification specs;
}