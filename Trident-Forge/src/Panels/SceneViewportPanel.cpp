#include "SceneViewportPanel.h"

#include <string>

namespace EditorPanels
{
    SceneViewportPanel::SceneViewportPanel()
    {

    }

    void SceneViewportPanel::Render()
    {

    }

    void SceneViewportPanel::Update()
    {
        // Handle ImGui drag-and-drop payloads that originate from inside the editor (e.g., content browser).
        if (m_OnAssetsDropped && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const std::string l_Path(reinterpret_cast<const char*>(l_Payload->Data), l_Payload->DataSize);
                m_OnAssetsDropped({ l_Path });
            }

            ImGui::EndDragDropTarget();
        }
    }

    glm::vec2 SceneViewportPanel::GetViewportSize() const
    {
        return glm::vec2();
    }

    bool SceneViewportPanel::IsHovered() const
    {
        return m_IsHovered;
    }

    bool SceneViewportPanel::IsFocused() const
    {
        return m_IsFocused;
    }

    bool SceneViewportPanel::ContainsPoint(const ImVec2& screenPoint) const
    {
        return screenPoint.x >= m_BoundsMin.x && screenPoint.x <= m_BoundsMax.x && screenPoint.y >= m_BoundsMin.y && screenPoint.y <= m_BoundsMax.y;
    }

    Trident::ECS::Entity SceneViewportPanel::GetSelectedEntity() const
    {
        return m_SelectedEntity;
    }

    void SceneViewportPanel::SetGizmoState(Trident::GizmoState* gizmoState)
    {
        m_GizmoState = gizmoState;
    }

    void SceneViewportPanel::SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped)
    {
        m_OnAssetsDropped = onAssetsDropped;
    }

    void SceneViewportPanel::SetSelectedEntity(Trident::ECS::Entity entity)
    {
        m_SelectedEntity = entity;
    }

    void SceneViewportPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    void SceneViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {

    }
}