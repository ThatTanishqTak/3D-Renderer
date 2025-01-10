#pragma once

#include "imgui-master/imgui.h"
#include "rlImGui.h"

namespace Engine
{
	class EnableDockspace
	{
	public:
		bool run = true;
		bool showDemoWindow = true;

		EnableDockspace()
		{
#ifdef IMGUI_HAS_DOCK
			ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode); // set ImGuiDockNodeFlags_PassthruCentralNode so that we can see the raylib contents behind the dockspace
#endif

			// show a simple menu bar
			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Quit"))
						run = false;

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Window"))
				{
					if (ImGui::MenuItem("Demo Window", nullptr, showDemoWindow))
						showDemoWindow = !showDemoWindow;

					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}

			// show some windows

			if (showDemoWindow)
				ImGui::ShowDemoWindow(&showDemoWindow);

			if (ImGui::Begin("Test Window"))
			{
				ImGui::TextUnformatted("Another window");
			}
			ImGui::End();
		}
	};
}