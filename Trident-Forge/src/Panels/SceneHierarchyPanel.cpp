#include "SceneHierarchyPanel.h"

#include <imgui.h>

namespace EditorPanels
{
    void SceneHierarchyPanel::Render()
    {
        if (ImGui::Begin("Scene Hierarchy"))
        {
            ImGui::TextUnformatted(m_StatusMessage.c_str());
            ImGui::TextUnformatted("Hierarchy population will be driven by the active registry.");
        }

        ImGui::End();
    }
}