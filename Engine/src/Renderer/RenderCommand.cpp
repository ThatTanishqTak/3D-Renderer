#include "RenderCommand.h"

#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

namespace Engine
{
	void RenderCommnad::Init()
	{
		//ModelLoader("Model/1.obj");
		//TextureLoader("Model/1.png");
	}

	void RenderCommnad::Shutdown()
	{

	}

	void RenderCommnad::RenderGrid()
	{
		DrawGrid(100, 1.0f);
	}

	void RenderCommnad::RenderUI()
	{
		PropertiesPanel();
	}

	void RenderCommnad::RenderScene()
	{
		DrawCube(ModelSpecs.Position, 10.0f, 10.0f, 10.0f, RED);
		//DrawModelEx(m_Model, ModelSpecs.Position, ModelSpecs.RotationAxis, ModelSpecs.RotationAngle, ModelSpecs.Scale, ModelSpecs.Tint);
	}

	void RenderCommnad::ModelLoader(std::filesystem::path filePath)
	{
		std::string string = filePath.string();
		const char* constChar = string.c_str();

		m_Model = LoadModel(constChar);
	}

	void RenderCommnad::TextureLoader(std::filesystem::path filePath)
	{
		std::string string = filePath.string();
		const char* constCahr = string.c_str();

		m_Texture = LoadTexture(constCahr);
	}

	void RenderCommnad::PropertiesPanel()
	{
		GuiPanel({ 0.0f, 0.0f, 250.0f, (float)m_Window.m_WindowHeight }, "Properties Panel");
	}
}