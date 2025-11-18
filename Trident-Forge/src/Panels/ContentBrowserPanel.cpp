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
        if (ImGui::Begin("Content Browser"))
        {
            ImGui::Text("Current directory: %s", m_CurrentDirectory.c_str());
            ImGui::TextUnformatted("Asset listing will be populated once the import pipeline is wired up.");
        }

        ImGui::End();
    }
}