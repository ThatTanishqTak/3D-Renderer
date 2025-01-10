#include "UserInterface.h"
#include "EnableDockspace.h"

#include "imgui-master/imgui.h"
#include "rlImGui.h"

namespace Engine
{
	void UserInterface::Init()
	{
		rlImGuiSetup(true);
	}

	void UserInterface::Shutdown()
	{
		rlImGuiShutdown();
	}

	void UserInterface::Update()
	{
		rlImGuiBegin();

		EnableDockspace enableDockspace;

		ImGui::Begin("Settings");

		ImGui::DragFloat("X", &m_ModelSpecification.Position.x);
		ImGui::DragFloat("Y", &m_ModelSpecification.Position.y);
		ImGui::DragFloat("Z", &m_ModelSpecification.Position.z);

		ImGui::End();

		rlImGuiEnd();
	}
}