#include "RenderCommand.h"
#include "imgui-master/imgui.h"
#include "rlImGui.h"

namespace Engine
{
	void RenderCommnad::Init()
	{
		//ModelLoader("Model/1.obj");
		//TextureLoader("Model/1.png");

		m_PanelWidth = 250.0f;
		m_PanelHeight = static_cast<float>(m_ApplicationSpecs.Height);

		m_UserInterface.Init();
	}

	void RenderCommnad::Shutdown()
	{
		m_UserInterface.Shutdown();
	}

	void RenderCommnad::RenderGrid()
	{
		DrawGrid(100, 1.0f);
	}

	void RenderCommnad::RenderUI()
	{
		m_UserInterface.Update();
	}

	void RenderCommnad::RenderScene()
	{
		DrawCube(m_ModelSpecs.Position, 10.0f, 10.0f, 10.0f, RED);
		//DrawModelEx(m_Model, ModelSpecs.Position, ModelSpecs.RotationAxis, ModelSpecs.RotationAngle, ModelSpecs.Scale, ModelSpecs.Tint);
	}

	void RenderCommnad::UpdateUI()
	{

	}
}