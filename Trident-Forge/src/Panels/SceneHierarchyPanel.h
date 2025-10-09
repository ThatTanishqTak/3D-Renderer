#pragma once

#include <limits>

#include "ECS/Registry.h"

namespace UI
{
    /**
     * @brief Panel responsible for displaying and editing the scene hierarchy.
     */
    class SceneHierarchyPanel
    {
    public:
        SceneHierarchyPanel();

        /**
         * @brief Configure which entity is currently highlighted in the hierarchy.
         */
        void SetSelectedEntity(Trident::ECS::Entity a_SelectedEntity);

        /**
         * @brief Retrieve the entity that is selected after the latest frame.
         */
        [[nodiscard]] Trident::ECS::Entity GetSelectedEntity() const;

        /**
         * @brief Draw the hierarchy tree and creation controls.
         */
        void Render();

    private:
        void DrawEntityList(Trident::ECS::Registry& a_Registry);
        void DrawLightCreationButtons(Trident::ECS::Registry& a_Registry);

        Trident::ECS::Entity m_SelectedEntity;
    };
}