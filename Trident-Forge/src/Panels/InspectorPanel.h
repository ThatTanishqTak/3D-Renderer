#pragma once

#include "GizmoState.h"
#include "ECS/Registry.h"

#include <string>

namespace EditorPanels
{
    /**
     * @brief Shows the properties for the currently selected entity.
     */
    class InspectorPanel
    {
    public:
        void Update();
        void Render();

        void SetGizmoState(Trident::GizmoState* gizmoState);
        void SetRegistry(Trident::ECS::Registry* registry);
        void SetSelectedEntity(Trident::ECS::Entity entity);
        void SetSelectionLabel(const std::string& label);

    private:
        Trident::GizmoState* m_GizmoState = nullptr; ///< Shared gizmo state pointer to align viewport and inspector modes.
        Trident::ECS::Registry* m_Registry = nullptr; ///< Registry pointer used to query components for the selected entity.
        Trident::ECS::Entity m_SelectedEntity = 0; ///< Currently selected entity whose components will be displayed.
        std::string m_SelectedLabel = "None"; ///< Cached display label describing the current selection.
    };
}