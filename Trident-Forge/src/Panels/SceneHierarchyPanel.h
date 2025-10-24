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
    // Expose the currently focused entity so companion panels (e.g. the inspector)
    // can surface component data without duplicating hierarchy logic.
    Trident::ECS::Entity GetSelectedEntity() const;

    void SetContextMenuHandler(std::function<void(const ImVec2&, const ImVec2&)> contextMenuHandler);

private:
    // Helper that renders a single entity row, including tag display and selection logic.
    void DrawEntityNode(Trident::ECS::Entity entity, Trident::ECS::Registry& registry);

private:
    // Sentinel value indicating that no entity is currently selected.
    Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    // Callback surfaced immediately after drawing so overlays can register context menus.
    std::function<void(const ImVec2&, const ImVec2&)> m_OnSceneHierarchyContextMenu{};

};