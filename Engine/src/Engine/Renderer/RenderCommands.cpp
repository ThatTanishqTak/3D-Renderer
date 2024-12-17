#include "RenderCommands.h"

#include <filesystem>
#include <raylib.h>

namespace Engine
{
	Vector3 g_Position = { 0.0f, 0.0f, 0.0f };
	Model g_Model;
	Texture2D g_Texture;
	bool show = true;

	void RenderCommands::DrawGame()
	{
		DrawGrid(1000, 1.0f);
	}

	void RenderCommands::DrawUI()
	{
		m_UserInterface.Update();
	}

	void RenderCommands::ModelLoading(std::filesystem::path filePath)
	{
		std::string temp = filePath.string();
		const char* l_path = temp.c_str();
		
		g_Model = LoadModel(l_path);
	}

	void RenderCommands::ModelDrawing()
	{
		DrawModel(g_Model, g_Position, 1.0f, WHITE);
	}

	void RenderCommands::TextureLoading(std::filesystem::path filePath)
	{
		std::string temp = filePath.string();
		const char* l_path = temp.c_str();

		g_Texture = LoadTexture(l_path);
	}

	void RenderCommands::TextureUnloading()
	{
		UnloadTexture(g_Texture);
	}

	void RenderCommands::ModelUnloading()
	{
		UnloadModel(g_Model);
	}
}