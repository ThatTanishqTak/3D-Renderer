#include "UserInterface.h"

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

		// Variables to configure the Dockspace example.
		static bool opt_fullscreen = true; // Is the Dockspace full-screen?
		static bool opt_padding = false; // Is there padding (a blank space) between the window edge and the Dockspace?
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None; // Config flags for the Dockspace
		static bool p_Open = true;

		// In this example, we're embedding the Dockspace into an invisible parent window to make it more configurable.
		// We set ImGuiWindowFlags_NoDocking to make sure the parent isn't dockable into because this is handled by the Dockspace.
		//
		// ImGuiWindowFlags_MenuBar is to show a menu bar with config options. This isn't necessary to the functionality of a
		// Dockspace, but it is here to provide a way to change the configuration flags interactively.
		// You can remove the MenuBar flag if you don't want it in your app, but also remember to remove the code which actually
		// renders the menu bar, found at the end of this function.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

		// Is the example in Fullscreen mode?
		if (opt_fullscreen)
		{
			// If so, get the main viewport:
			const ImGuiViewport* viewport = ImGui::GetMainViewport();

			// Set the parent window's position, size, and viewport to match that of the main viewport. This is so the parent window
			// completely covers the main viewport, giving it a "full-screen" feel.
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);

			// Set the parent window's styles to match that of the main viewport:
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f); // No corner rounding on the window
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); // No border around the window

			// Manipulate the window flags to make it inaccessible to the user (no titlebar, resize/move, or navigation)
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			// The example is not in Fullscreen mode (the parent window can be dragged around and resized), disable the
			// ImGuiDockNodeFlags_PassthruCentralNode flag.
			dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so the parent window should not have its own background:
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// If the padding option is disabled, set the parent window's padding size to 0 to effectively hide said padding.
		if (!opt_padding)
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		ImGui::Begin("DockSpace Demo", &p_Open, window_flags);

		// Remove the padding configuration - we pushed it, now we pop it:
		if (!opt_padding)
			ImGui::PopStyleVar();

		// Pop the two style rules set in Fullscreen mode - the corner rounding and the border size.
		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		// Check if Docking is enabled:
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			// If it is, draw the Dockspace with the DockSpace() function.
			// The GetID() function is to give a unique identifier to the Dockspace - here, it's "MyDockSpace".
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		}
		else
		{
			// Docking is DISABLED - Show a warning message
			
		}

		// This is to show the menu bar that will change the config settings at runtime.
		// If you copied this demo function into your own code and removed ImGuiWindowFlags_MenuBar at the top of the function,
		// you should remove the below if-statement as well.

		ImGui::DragFloat("X", &m_ModelSpecification.Position.x);
		ImGui::DragFloat("Y", &m_ModelSpecification.Position.y);
		ImGui::DragFloat("Z", &m_ModelSpecification.Position.z);

		ImGui::End();

		rlImGuiEnd();
	}
}