#include "RenderCommand.h"

#define RAYGUI_IMPLEMENTATION
#include <raygui.h>

namespace Engine
{
	void RenderCommnad::Init()
	{

	}

	void RenderCommnad::Shutdown()
	{

	}

	void RenderCommnad::RenderGrid()
	{
		if (m_Camera.GetCamera().position.x)
		{
			DrawGrid(100, 1.0f);
		}
	}

	void RenderCommnad::RenderUI()
	{
		if (GuiButton({ 24, 24, 120, 100 }, "#191#Show Message"))
		{
			showMessageBox = true;
		}

		if (showMessageBox)
		{
			int result = GuiMessageBox({ 85, 70, 250, 100 }, "#191#Message Box", "Hi! This is a message!", "Nice;Cool");
			if (result == 0)
			{
				showMessageBox = false;
			}
		}
	}

	void RenderCommnad::RenderScene()
	{
		DrawCube({ 0.0f, 0.0f, 0.0f }, 10.0f, 10.0f, 10.0f, RED);
	}

	void RenderCommnad::ModelLoader()
	{
		m_Model = LoadModel("");
	}
}