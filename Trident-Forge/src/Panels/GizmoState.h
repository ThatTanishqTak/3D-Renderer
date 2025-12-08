#pragma once

namespace Trident
{
    /**
     * @brief Shared gizmo configuration passed between panels to keep transform tool state synchronized.
     */
    struct GizmoState
    {
        bool m_ShowGizmos = true; // Controls whether gizmos are rendered in the viewport.
        bool m_TranslateEnabled = true; // Toggles translation handles for position adjustments.
        bool m_RotateEnabled = true; // Toggles rotation handles for orientation edits.
        bool m_ScaleEnabled = true; // Toggles scale handles for uniform/non-uniform scaling.
    };
}