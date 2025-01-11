#pragma once

#include <raylib.h>
#include <filesystem>

#include "../Application/WindowSpecification.h"
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

	private:
		Camera m_Camera;
		ModelSpecification m_ModelSpecs;
		WindowSpecification m_WindowSpecs;
		UserInterface m_UserInterface;

		Texture2D m_Texture;
		Model m_Model;
	};
}