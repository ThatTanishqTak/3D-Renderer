#include "RenderCommand.h"
#include "imgui-master/imgui.h"
#include "rlImGui.h"

namespace Engine
{
	void RenderCommnad::Init()
	{
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