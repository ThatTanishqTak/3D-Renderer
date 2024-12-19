#include "RenderCommands.h"

#include <string>
#include <filesystem>
#include <raylib.h>

namespace Engine
{
	void RenderCommands::DrawGame()
	{
		DrawGrid(1000, 1.0f);
	}

	void RenderCommands::DrawUI()
	{

	}

	void RenderCommands::ModelLoading(std::filesystem::path filePath)
	{

	}

	void RenderCommands::ModelDrawing()
	{

	}

	void RenderCommands::TextureLoading(std::filesystem::path filePath)
	{

	}

	void RenderCommands::TextureUnloading()
	{

	}

	void RenderCommands::ModelUnloading()
	{

	}
}