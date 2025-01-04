#pragma once

#include <raylib.h>
#include <string>

namespace Engine
{
	class ApplicationSpecification
	{
	public:
		int Width = 1080;
		int Height = 720;
		
		std::string Title = "3D Renderer";
		
		Image Icon = LoadImage("Resources/Icon/icon.png");
	};

	class ModelSpecification
	{
	public:
		Vector3 Position = { 0.0f, 0.0f, 0.0f };
		Vector3 RotationAxis = { 0.0f, 0.0f, 0.0f };
		Vector3 Scale = { 1.0f, 1.0f, 1.0f };
		
		float RotationAngle = 0.0f;
		
		Color Tint = WHITE;
	};

	class TextureSpecification
	{
	public:
		Vector2 Position = { 0.0f, 0.0f };

		float Rotation = 0.0f;
		float Scale = 1.0f;

		Color Tint = WHITE;
	};
}