#include "InspectorPanel.h"

#include <imgui.h>
#include <string>

namespace EditorPanels
{
    void InspectorPanel::Update()
    {
        // Rebuild the display label when the selection changes so Render can focus on UI presentation.
        if (m_SelectedEntity == 0)
        {
            m_SelectedLabel = "None";
        }
        else
        {
            m_SelectedLabel = "Entity " + std::to_string(m_SelectedEntity);
        }
    }

    void InspectorPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Inspector");
        (void)l_WindowVisible;
        // Submit inspector contents unconditionally so dockspace tests retain the node regardless of collapse.

        ImGui::Text("Selection: %s", m_SelectedLabel.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Component editing UI will appear here.");

        ImGui::End();
    }

    void InspectorPanel::SetSelectionLabel(const std::string& label)
    {
        m_SelectedLabel = label;
    }

    void InspectorPanel::SetGizmoState(Trident::GizmoState* gizmoState)
    {
        m_GizmoState = gizmoState;
    }

    void InspectorPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void InspectorPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }
}