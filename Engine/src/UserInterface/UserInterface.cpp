#include "UserInterface.h"
#include "EnableDockspace.h"

#include "imgui-master/imgui.h"
#include "rlImGui.h"

namespace Engine
{
	void UserInterface::Init()
	{
		rlImGuiSetup(true);
		ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	}

	void UserInterface::Shutdown()
	{
		rlImGuiShutdown();
	}

	void UserInterface::Update()
	{
		rlImGuiBegin();

		EnableDockspace enableDockspace;

		rlImGuiEnd();
	}
}