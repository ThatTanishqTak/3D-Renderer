#pragma once

#include <raylib.h>
#include <filesystem>

#include "../Application/Specification.h"
#include "../UserInterface/ModelLoader.h"
#include "../UserInterface/UserInterface.h"
#include "../Camera/Camera.h"

namespace Engine
{
	class RenderCommnad
	{
	public:
		void Init();
		void Shutdown();

		void RenderGrid();
		void RenderUI();
		void RenderScene();

		void UpdateUI();

	public:
		bool showMessageBox = false;

	private:
		Camera m_Camera;
		ModelSpecification m_ModelSpecs;
		ApplicationSpecification m_ApplicationSpecs;
		ModelLoader m_ModelLoader;
		UserInterface m_UserInterface;

		Texture2D m_Texture;
		Model m_Model;

		float m_PanelWidth;
		float m_PanelHeight;
	};
}