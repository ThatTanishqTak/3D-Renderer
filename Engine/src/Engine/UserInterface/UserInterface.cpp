#include "UserInterface.h"

#include <raylib.h>
#include <rlImGui.h>
#include <imgui.h>
#include <imgui_impl_raylib.h>

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
		ImGui::ShowDemoWindow();
		rlImGuiEnd();
	}
}