#pragma once

#include "ECS/Entity.h"

#include <limits>
#include <functional>

#include <imgui.h>

namespace Trident
{
    namespace ECS
    {
        class Registry;
    }
}

/**
 * SceneHierarchyPanel exposes the list of entities registered in the active scene.
 * The panel keeps lightweight state about the current selection so editor widgets can
 * highlight the focused entity. Runtime tools obtain the registry from Startup at
 * render time, which keeps the panel decoupled from scene ownership.
 */
class SceneHierarchyPanel
{
public:
    // Validate cached selection so the panel never references destroyed entities.
    void Update();
    // Draw the entity list inside an ImGui window and respond to user selection.
    void Render();

    // Allow the application layer to configure actions used by the hierarchy's context menu.
    void SetContextMenuActions(std::function<void()> createEmptyEntityAction, std::function<void()> createCubeAction, std::function<void()> createSphereAction,
        std::function<void()> createQuadAction);

    // can surface component data without duplicating hierarchy logic.
    Trident::ECS::Entity GetSelectedEntity() const;

private:
    // Helper that renders a single entity row, including tag display and selection logic.
    void DrawEntityNode(Trident::ECS::Entity entity, Trident::ECS::Registry& registry);

private:
    // Sentinel value indicating that no entity is currently selected.
    Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    // Editor-provided actions that allow the hierarchy to spawn new entities from its context menu.
    std::function<void()> m_CreateEmptyEntityAction;
    std::function<void()> m_CreateCubePrimitiveAction;
    std::function<void()> m_CreateSpherePrimitiveAction;
    std::function<void()> m_CreateQuadPrimitiveAction;
};