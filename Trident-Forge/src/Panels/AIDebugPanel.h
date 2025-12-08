#pragma once

#include <string>
#include <limits>

namespace Trident::ECS
{
    class Registry;
    using Entity = uint32_t;
}

namespace EditorPanels
{
    /**
     * @brief Presents AI telemetry for the selected entity.
     *
     * The panel currently surfaces placeholder data so integration with the runtime AI
     * systems can be layered on incrementally.
     */
    class AIDebugPanel
    {
    public:
        void Initialize();
        void Update();
        void Render();

        /**
         * @brief Bind the panel to the active registry without taking ownership.
         *
         * The pointer remains valid as long as the application layer keeps the registry alive.
         */
        void SetRegistry(Trident::ECS::Registry* registry);
        void SetSelectedEntity(Trident::ECS::Entity entity);

    private:
        static constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max(); ///< Sentinel indicating no selection has been provided.

        Trident::ECS::Registry* m_Registry = nullptr;
        Trident::ECS::Entity m_SelectedEntity = s_InvalidEntity;
        std::string m_DebugSummary = "No selection";
    };
}