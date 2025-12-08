#pragma once

#include "GizmoState.h"
#include "ECS/Registry.h"

#include <string>
#include <limits>

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
        static constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max(); ///< Sentinel used to represent the absence of a selection.

        Trident::GizmoState* m_GizmoState = nullptr; ///< Shared gizmo state pointer to align viewport and inspector modes.
        Trident::ECS::Registry* m_Registry = nullptr; ///< Registry pointer used to query components for the selected entity.
        Trident::ECS::Entity m_SelectedEntity = s_InvalidEntity; ///< Currently selected entity whose components will be displayed.
        std::string m_SelectedLabel = "None"; ///< Cached display label describing the current selection.
    };
}