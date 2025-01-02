#pragma once

#include <raylib.h>
#include <filesystem>

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

	public:
		bool showMessageBox = false;

	private:
		void ModelLoader(std::filesystem::path filePath);
		void TextureLoader(std::filesystem::path filePath);

		void PropertiesPanel();

	private:
		Camera m_Camera;

		Model m_Model;
		struct ModelSpecification
		{

			Vector3 Position = { 0.0f, 0.0f, 0.0f };
			Vector3 RotationAxis = { 0.0f, 0.0f, 0.0f };
			float RotationAngle = 0.0f;
			Vector3 Scale = { 1.0f, 1.0f, 1.0f };
			Color Tint = WHITE;
		};
		ModelSpecification ModelSpecs;

		Texture2D m_Texture;
		struct TextureSpecification
		{
			Vector2 Position = { 0.0f, 0.0f };
			float Rotation = 0.0f;
			float Scale = 1.0f;
			Color Tint = WHITE;
		};
		TextureSpecification TextureSpecs;
	};
}