#include "ContentBrowserPanel.h"

#include <imgui.h>

namespace EditorPanels
{
    void ContentBrowserPanel::Update()
    {
        // Placeholder hook for future asset indexing or selection caching logic.
    }

    void ContentBrowserPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Content Browser");
        (void)l_WindowVisible;
        // Keep submission unconditional so dockspace layouts exercise the window presence every frame.

        ImGui::Text("Current directory: %s", m_CurrentDirectory.c_str());
        ImGui::TextUnformatted("Asset listing will be populated once the import pipeline is wired up.");

        ImGui::End();
    }
}