#pragma once

#include "../GUI/GUI.h"

#include <filesystem>

namespace Engine
{
	class RenderCommands
	{
	public:
		void DrawGame();
		void DrawUI();

		void ModelLoading(std::filesystem::path filePath);
		void ModelDrawing();

		void TextureLoading(std::filesystem::path filePath);

		void TextureUnloading();
		void ModelUnloading();

	public:
		GUI m_Gui;
	};
}