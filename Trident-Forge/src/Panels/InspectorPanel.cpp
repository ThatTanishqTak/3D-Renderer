#include "InspectorPanel.h"

#include <imgui.h>

namespace EditorPanels
{
    void InspectorPanel::Render()
    {
        if (ImGui::Begin("Inspector"))
        {
            ImGui::Text("Selection: %s", m_SelectedLabel.c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("Component editing UI will appear here.");
        }

        ImGui::End();
    }

    void InspectorPanel::SetSelectionLabel(const std::string& label)
    {
        m_SelectedLabel = label;
    }
}