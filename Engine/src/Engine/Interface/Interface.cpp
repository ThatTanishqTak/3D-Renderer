#include "Interface.h"

#include <rlImGui.h>
#include <imgui_impl_raylib.h>
#include <raylib.h>

namespace Engine
{
	bool show = true;

	void Interface::Init()
	{
		rlImGuiSetup(true);
	}

	void Interface::Shutdown()
	{
		rlImGuiShutdown();
	}

	void Interface::Render()
	{
		rlImGuiBegin();

		ImGui::Begin("Test Window");

		ImGui::End();

		rlImGuiEnd();
	}
}