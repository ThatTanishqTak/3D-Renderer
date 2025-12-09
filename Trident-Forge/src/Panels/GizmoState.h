#pragma once

namespace Trident
{
    /**
     * @brief Shared gizmo configuration passed between panels to keep transform tool state synchronized.
     */
    struct GizmoState
    {
        bool m_ShowGizmos = true; // Controls whether gizmos are rendered in the viewport.
        bool m_TranslateEnabled = true; // Only translation is active by default so tools start in a predictable state.
        bool m_RotateEnabled = false; // Rotation toggles off so the gizmo operates in a single mode at a time.
        bool m_ScaleEnabled = false; // Scale toggles off so translate/rotate/scale remain mutually exclusive.
    };
}