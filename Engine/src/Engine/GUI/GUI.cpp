#include "GUI.h"

#include <raylib.h>
#include <rlImGui.h>
#include <imgui.h>
#include <imgui_impl_raylib.h>

namespace Engine
{
	void GUI::Init()
	{
		rlImGuiSetup(true);
	}

	void GUI::Shutdown()
	{
		rlImGuiShutdown();
	}

	void GUI::Update()
	{
		rlImGuiBegin();
		ImGui::ShowDemoWindow();
		rlImGuiEnd();
	}
}