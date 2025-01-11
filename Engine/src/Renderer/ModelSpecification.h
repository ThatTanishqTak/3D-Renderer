#pragma once

#include <raylib.h>

namespace Engine
{
	class ModelSpecification
	{
	public:
		Vector3 Position = { 0.0f, 0.0f, 0.0f };
		Vector3 RotationAxis = { 0.0f, 0.0f, 0.0f };
		Vector3 Scale = { 1.0f, 1.0f, 1.0f };

		float RotationAngle = 0.0f;

		Color Tint = WHITE;
	};
}