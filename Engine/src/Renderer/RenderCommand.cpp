#include "RenderCommand.h"
#include <raygui-cpp.h>

namespace Engine
{
	void RenderCommnad::Init()
	{
		//ModelLoader("Model/1.obj");
		//TextureLoader("Model/1.png");

		m_PanelWidth = 250.0f;
		m_PanelHeight = static_cast<float>(m_ApplicationSpecs.Height);
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
		
	}

	void RenderCommnad::RenderScene()
	{
		DrawCube(m_ModelSpecs.Position, 10.0f, 10.0f, 10.0f, RED);
		//DrawModelEx(m_Model, ModelSpecs.Position, ModelSpecs.RotationAxis, ModelSpecs.RotationAngle, ModelSpecs.Scale, ModelSpecs.Tint);
	}

	void RenderCommnad::UpdateUI()
	{

	}

	//void RenderCommnad::PropertiesPanel()
	//{
	//	auto button = rgc::Button(rgc::Bounds::WithText("TEST", 22, { 15, 15 }), "TEST");
	//	button.SetStyle(rgc::Style(rgc::Style::Position::TOP_LEFT, { 0, 0 }));
	//	button.OnClick([&button]()
	//		{

	//		});

	//	button.Update();
	//	button.Show();
	//}
}