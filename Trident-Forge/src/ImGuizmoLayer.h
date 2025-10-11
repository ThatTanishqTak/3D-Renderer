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

private:
    ImGuizmo::OPERATION m_GizmoOperation; // Current gizmo operation mode tracked by the layer
    ImGuizmo::MODE m_GizmoMode; // Current gizmo orientation mode tracked by the layer
};