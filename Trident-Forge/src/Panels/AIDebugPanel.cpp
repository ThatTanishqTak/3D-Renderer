#include "AIDebugPanel.h"

#include <imgui.h>

namespace EditorPanels
{
    void AIDebugPanel::Initialize()
    {
        // Future improvement: pull live AI metrics from the selected entity's components.
        m_DebugSummary = "AI debug panel initialized";
    }

    void AIDebugPanel::Update()
    {
        // Without a runtime AI system the panel reports basic selection context.
        if (m_SelectedEntity == 0)
        {
            m_DebugSummary = "No entity selected";
        }
        else
        {
            m_DebugSummary = "Selected entity ID: " + std::to_string(m_SelectedEntity);
        }
    }

    void AIDebugPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("AI Debug");
        (void)l_WindowVisible;
        // Keep the window submission unconditional so dockspace layouts stay stable when toggling visibility.

        ImGui::TextWrapped("AI Diagnostics");
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_DebugSummary.c_str());

        ImGui::End();
    }

    void AIDebugPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        // Non-owning: the application layer manages registry lifetime and swaps it when play mode toggles.
        m_Registry = registry;
    }

    void AIDebugPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }
}