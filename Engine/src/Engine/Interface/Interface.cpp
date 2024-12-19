#include "Interface.h"

#include <raylib.h>
#include <rlImGui.h>
#include <imgui_impl_raylib.h>

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

	void Interface::Update()
	{
		rlImGuiBegin();

		ImGui::ShowDemoWindow(&show);

		rlImGuiEnd();
	}
}