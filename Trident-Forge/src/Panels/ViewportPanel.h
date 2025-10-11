#pragma once

#include <string>
#include <limits>

#include "ECS/Entity.h"

namespace UI
{
    /**
     * @brief Scene viewport wrapper responsible for drawing the rendered output and viewport controls.
     *
     * The goal is to gradually lift UI responsibilities out of ApplicationLayer so panels become
     * self-contained. The viewport starts this effort by owning camera selection and overlay drawing.
     */
    class ViewportPanel
    {
    public:
        ViewportPanel();

        /**
         * @brief Update the entity highlighted in the viewport overlay.
         */
        void SetSelectedEntity(Trident::ECS::Entity selectedEntity);

        /**
         * @brief Publish the entity currently highlighted by the viewport so other panels can mirror the state.
         */
        [[nodiscard]] Trident::ECS::Entity GetSelectedEntity() const;


        /**
         * @brief Publish the currently assigned viewport camera for downstream systems (e.g. gizmos).
         */
        [[nodiscard]] Trident::ECS::Entity GetSelectedCamera() const;

        /**
         * @brief Draw the viewport panel and its immediate controls.
         */
        void Render();

    private:
        void HandleAssetDrop(const std::string& path);

    private:
        static constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

        Trident::ECS::Entity m_SelectedViewportCamera;
        Trident::ECS::Entity m_SelectedEntity;
        int m_SelectedCameraIndex;
    };
}