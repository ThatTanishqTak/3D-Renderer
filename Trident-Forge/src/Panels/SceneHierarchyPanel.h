#pragma once

#include "ECS/Registry.h"

#include <functional>
#include <string>

namespace EditorPanels
{
    /**
     * @brief Lists entities in the active scene.
     */
    class SceneHierarchyPanel
    {
    public:
        void Update();
        void Render();

        void SetRegistry(Trident::ECS::Registry* registry);
        void SetContextMenuActions(const std::function<void()>& onCreateEmpty,
            const std::function<void()>& onCreateCube,
            const std::function<void()>& onCreateSphere,
            const std::function<void()>& onCreateQuad);

        [[nodiscard]] Trident::ECS::Entity GetSelectedEntity() const;
        void SetSelectedEntity(Trident::ECS::Entity entity);

    private:
        Trident::ECS::Registry* m_Registry = nullptr; ///< Active registry used to populate the hierarchy listing.
        Trident::ECS::Entity m_SelectedEntity = 0; ///< Currently selected entity ID mirrored into the inspector.
        std::string m_StatusMessage = "No entities registered"; ///< UI string explaining the current population status.
        std::function<void()> m_OnCreateEmpty; ///< Callback used to spawn an empty entity from the context menu.
        std::function<void()> m_OnCreateCube; ///< Callback used to spawn a cube primitive from the context menu.
        std::function<void()> m_OnCreateSphere; ///< Callback used to spawn a sphere primitive from the context menu.
        std::function<void()> m_OnCreateQuad; ///< Callback used to spawn a quad primitive from the context menu.
    };
}