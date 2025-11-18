#pragma once

#include "ECS/Scene.h"

#include <limits>
#include <string>

namespace Trident::ECS
{
    class Registry;
    using Entity = uint32_t;
}

namespace EditorPanels
{
    /**
     * @brief Visualises animation graph data for the selected entity.
     *
     * The panel currently reports high-level placeholders so later animation tooling can
     * attach graph nodes, state machines, and preview controls.
     */
    class AnimationGraphPanel
    {
    public:
        void Initialize();
        void Update();
        void Render();

        /**
         * @brief Bind the panel to the active registry without taking ownership.
         *
         * The registry is owned and swapped by the application layer when entering or leaving play mode.
         */
        void SetRegistry(Trident::ECS::Registry* registry);
        void SetSelectedEntity(Trident::ECS::Entity entity);

    private:
        Trident::ECS::Registry* m_Registry = nullptr;
        Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
        std::string m_StatusText = "No animation data";
    };
}