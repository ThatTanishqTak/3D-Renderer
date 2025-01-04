#pragma once

#include <raylib.h>
#include <filesystem>

#include "../Application/Specification.h"
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
		ModelSpecification m_ModelSpecs;
		ApplicationSpecification m_ApplicationSpecs;

		Texture2D m_Texture;
		Model m_Model;
	};
}