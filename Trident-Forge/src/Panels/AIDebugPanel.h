#pragma once

#include <string>

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
        Trident::ECS::Registry* m_Registry = nullptr;
        Trident::ECS::Entity m_SelectedEntity = 0;
        std::string m_DebugSummary = "No selection";
    };
}