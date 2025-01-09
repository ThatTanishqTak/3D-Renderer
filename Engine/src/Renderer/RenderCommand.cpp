#include "RenderCommand.h"
#include "rlImGui.h"
#include "imgui-master/imgui.h"

namespace Engine
{
	void RenderCommnad::Init()
	{
		//ModelLoader("Model/1.obj");
		//TextureLoader("Model/1.png");

		m_PanelWidth = 250.0f;
		m_PanelHeight = static_cast<float>(m_ApplicationSpecs.Height);

		rlImGuiSetup(true);
	}

	void RenderCommnad::Shutdown()
	{
		rlImGuiShutdown();
	}

	void RenderCommnad::RenderGrid()
	{
		DrawGrid(100, 1.0f);
	}

	void RenderCommnad::RenderUI()
	{
		rlImGuiBegin();

		bool open = true;
		ImGui::ShowDemoWindow(&open);
	
		rlImGuiEnd();
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