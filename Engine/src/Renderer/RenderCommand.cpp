#include "RenderCommand.h"
#include <raygui-cpp.h>

namespace Engine
{
	rgc::Button button;

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
		button.Show();
	}

	void RenderCommnad::RenderScene()
	{
		DrawCube(m_ModelSpecs.Position, 10.0f, 10.0f, 10.0f, RED);
		//DrawModelEx(m_Model, ModelSpecs.Position, ModelSpecs.RotationAxis, ModelSpecs.RotationAngle, ModelSpecs.Scale, ModelSpecs.Tint);
	}

	void RenderCommnad::UpdateUI()
	{
		ModelLoader m_ModelLoader;

		button = rgc::Button(rgc::Bounds(0.0f, 0.0f, 100.0f, 100.0f), "TEST");
		button.OnClick([&m_ModelLoader]()
			{
				m_ModelLoader.OpenFileDialog();
			});
	}
}