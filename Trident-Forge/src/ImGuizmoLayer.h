#pragma once

#include "ECS/Entity.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace UI
{
    class InspectorPanel;
    class ViewportPanel;
}

/**
 * @brief Captures interaction details for the current ImGuizmo frame so that other UI systems
 *        can react to manipulator usage without querying ImGuizmo directly.
 */
struct ImGuizmoInteractionState
{
    bool Hovered = false; // True when the cursor is hovering a gizmo handle this frame
    bool Active = false; // True when the user is actively dragging a gizmo handle this frame
};

/**
 * @brief Encapsulates all ImGuizmo state handling and rendering for the editor viewport.
 */
class ImGuizmoLayer
{
public:
    ImGuizmoLayer();

    /**
     * @brief Share ImGuizmo state pointers with the inspector so the UI can drive gizmo behavior.
     */
    void Initialize(UI::InspectorPanel& inspectorPanel);

    /**
     * @brief Render the transform gizmo for the currently selected entity.
     */
    void Render(Trident::ECS::Entity selectedEntity, UI::ViewportPanel& viewportPanel);

    /**
     * @brief Publish the gizmo interaction state captured during the most recent render call.
     */
    [[nodiscard]] ImGuizmoInteractionState GetInteractionState() const;

private:
    ImGuizmo::OPERATION m_GizmoOperation; // Current gizmo operation mode tracked by the layer
    ImGuizmo::MODE m_GizmoMode; // Current gizmo orientation mode tracked by the layer
    ImGuizmoInteractionState m_InteractionState; // Hover/active state recorded for the current frame
};