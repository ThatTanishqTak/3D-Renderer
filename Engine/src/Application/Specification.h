#pragma once

#include <raylib.h>
#include <string>

namespace Engine
{
	struct ApplicationSpecification
	{
		int Width = 1920;
		int Height = 1080;
		
		std::string Title = "3D Renderer";
		
		Image Icon = LoadImage("");
	};

	ApplicationSpecification specs;
}