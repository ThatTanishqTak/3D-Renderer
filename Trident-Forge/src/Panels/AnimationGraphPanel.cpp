#include "AnimationGraphPanel.h"

#include <imgui.h>

namespace EditorPanels
{
    void AnimationGraphPanel::Initialize()
    {
        // Initial status keeps the UI clear until a selection is provided.
        m_StatusText = "Waiting for entity selection";
    }

    void AnimationGraphPanel::Update()
    {
        // Future improvement: query the registry for animation components and populate node previews.
        if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
        {
            m_StatusText = "No entity selected";
        }
        else
        {
            m_StatusText = "Animation graph not available for entity " + std::to_string(m_SelectedEntity);
        }
    }

    void AnimationGraphPanel::Render()
    {
        if (!ImGui::Begin("Animation Graph"))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Animation Graph Overview");
        ImGui::Separator();
        ImGui::TextWrapped("%s", m_StatusText.c_str());

        ImGui::End();
    }

    void AnimationGraphPanel::SetRegistry(Trident::ECS::Registry** registry)
    {
        m_Registry = registry;
    }

    void AnimationGraphPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }
}