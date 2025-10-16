#pragma once

#include <ImGuizmo.h>

/**
 * GizmoState centralises the transform gizmo configuration so multiple panels can
 * coordinate how the gizmo behaves without duplicating state.
 */
class GizmoState
{
public:
    /**
     * Store the gizmo operation that should be active (translate, rotate, scale, etc.).
     */
    void SetOperation(ImGuizmo::OPERATION operation)
    {
        m_Operation = operation;
    }

    /**
     * Retrieve the gizmo operation currently active.
     */
    ImGuizmo::OPERATION GetOperation() const
    {
        return m_Operation;
    }

    /**
     * Store the space the gizmo should operate in (local or world).
     */
    void SetMode(ImGuizmo::MODE mode)
    {
        m_Mode = mode;
    }

    /**
     * Retrieve the gizmo space currently selected.
     */
    ImGuizmo::MODE GetMode() const
    {
        return m_Mode;
    }

    /**
     * Toggle whether the gizmo should be displayed this frame based on entity selection.
     */
    void SetSelectionActive(bool hasSelection)
    {
        m_HasSelection = hasSelection;
    }

    /**
     * Determine if the gizmo should render because a valid selection exists.
     */
    bool HasSelection() const
    {
        return m_HasSelection;
    }

private:
    // Persist the gizmo's current operation so it stays in sync across panels.
    ImGuizmo::OPERATION m_Operation = ImGuizmo::TRANSLATE;
    // Persist the gizmo's current space so users can switch between local/world axes.
    ImGuizmo::MODE m_Mode = ImGuizmo::LOCAL;
    // Track whether the editor currently has a selection to manipulate.
    bool m_HasSelection = false;
};