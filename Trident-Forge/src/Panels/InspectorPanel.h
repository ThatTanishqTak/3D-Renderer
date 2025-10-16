#pragma once

#include "ECS/Entity.h"

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
    // Verify the selection is still valid and avoid dereferencing stale data.
    void Update();
    // Draw the inspector window and surface component-specific widgets.
    void Render();

private:
    // Helper that draws a read-only tag field when present on the entity.
    void DrawTagComponent(Trident::ECS::Registry& registry);
    // Helper that exposes transform controls for translation, rotation, and scale.
    void DrawTransformComponent(Trident::ECS::Registry& registry);

    // Sentinel used to represent the absence of a valid selection.
    Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
};