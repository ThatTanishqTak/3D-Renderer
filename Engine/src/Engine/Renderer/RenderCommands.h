#pragma once

#include <filesystem>

namespace Engine
{
	class RenderCommands
	{
	public:
		void DrawGame();

		void ModelLoading(std::filesystem::path filePath);
		void ModelDrawing();

		void TextureLoading(std::filesystem::path filePath);

		void TextureUnloading();
		void ModelUnloading();
	};
}