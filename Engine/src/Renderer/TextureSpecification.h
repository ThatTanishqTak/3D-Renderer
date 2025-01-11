#pragma once

#include <raylib.h>

namespace Engine
{
	class TextureSpecification
	{
	public:
		Vector2 Position = { 0.0f, 0.0f };

		float Rotation = 0.0f;
		float Scale = 1.0f;

		Color Tint = WHITE;
	};
}