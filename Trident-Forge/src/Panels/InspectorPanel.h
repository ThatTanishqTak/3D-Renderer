#pragma once

#include "ECS/Entity.h"
#include "GizmoState.h"

#include <array>
#include <limits>

namespace Trident
{
    namespace ECS
    {
        class Registry;
    }
}

/**
 * InspectorPanel renders contextual component information for the entity
 * highlighted within the SceneHierarchyPanel. The panel only caches the
 * selected entity identifier; all component data is read directly from the
 * runtime registry each frame so edits elsewhere remain immediately visible.
 */
class InspectorPanel
{
public:
    // Accept the entity from the hierarchy before Update() runs so the
    // inspector can validate it against the registry.
    void SetSelectedEntity(Trident::ECS::Entity entity);
    // Provide access to the shared gizmo state so the inspector can drive configuration widgets.
    void SetGizmoState(GizmoState* gizmoState);
    // Verify the selection is still valid and avoid dereferencing stale data.
    void Update();
    // Draw the inspector window and surface component-specific widgets.
    void Render();

private:
    // Helper that draws a read-only tag field when present on the entity.
    void DrawTagComponent(Trident::ECS::Registry& registry);
    // Helpers that expose optional component data and support removal.
    void DrawTransformComponent(Trident::ECS::Registry& registry);
    void DrawCameraComponent(Trident::ECS::Registry& registry);
    void DrawLightComponent(Trident::ECS::Registry& registry);
    void DrawMeshComponent(Trident::ECS::Registry& registry);
    void DrawSpriteComponent(Trident::ECS::Registry& registry);
    void DrawScriptComponent(Trident::ECS::Registry& registry);
    // Helper that renders the add component popup and handles the search workflow.
    void DrawAddComponentMenu(Trident::ECS::Registry& registry);
    // Helper that runs a case-insensitive contains check against the search buffer.
    bool PassesAddComponentFilter(const char* componentName) const;

private:
    // Sentinel used to represent the absence of a valid selection.
    Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    // Shared gizmo configuration that mirrors the viewport overlay behaviour.
    GizmoState* m_GizmoState = nullptr;
    // Size of the persistent add component search buffer.
    static constexpr std::size_t s_AddComponentSearchBufferSize = 128;
    // Backing buffer that stores the current search query typed inside the popup.
    std::array<char, s_AddComponentSearchBufferSize> m_AddComponentSearchBuffer{};
    // When true the next popup open will focus the search field for rapid workflows.
    bool m_ShouldFocusAddComponentSearch = false;
};