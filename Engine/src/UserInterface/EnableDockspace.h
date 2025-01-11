#pragma once

#include "../Application/Window.h"

#include "imgui-master/imgui.h"
#include "rlImGui.h"

namespace Engine
{
	Window m_Window;
	
	class EnableDockspace
	{
	public:

		EnableDockspace()
		{
			ImGui::DockSpaceOverViewport(0, NULL, ImGuiDockNodeFlags_PassthruCentralNode);

			if (ImGui::BeginMainMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Quit"))
					{
						m_Window.m_IsRunning = false;
					}

					ImGui::EndMenu();
				}

				ImGui::EndMainMenuBar();
			}
		}
	};
}