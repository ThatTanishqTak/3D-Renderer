#pragma once

#include <array>
#include <string>

#include "ECS/Registry.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace UI
{
    /**
     * @brief Panel that exposes component properties for the selected entity.
     */
    class InspectorPanel
    {
    public:
        InspectorPanel();

        /**
         * @brief Update which entity's details should be displayed.
         */
        void SetSelectedEntity(Trident::ECS::Entity selectedEntity);

        /**
         * @brief Provide access to the gizmo state so the inspector can drive ImGuizmo controls.
         */
        void SetGizmoState(ImGuizmo::OPERATION* operation, ImGuizmo::MODE* mode);

        /**
         * @brief Draw all component editors and helper dialogs.
         */
        void Render();

    private:
        void DrawTransformSection(Trident::ECS::Registry& registry);
        void DrawCameraSection(Trident::ECS::Registry& registry);
        void DrawSpriteSection(Trident::ECS::Registry& registry);
        void DrawLightSection(Trident::ECS::Registry& registry);
        void DrawGizmoControls();
        void DrawEditorCameraSection();
        void DrawMaterialsSection();
        void DrawLiveReloadSection();

        void ResetCameraCacheIfNeeded(Trident::ECS::Entity selectedEntity, Trident::ECS::Registry& registry);
        void ResetSpriteCacheIfNeeded(Trident::ECS::Entity selectedEntity, Trident::ECS::Registry& registry);

        Trident::ECS::Entity m_SelectedEntity;

        std::array<char, 128> m_CameraNameBuffer;
        Trident::ECS::Entity m_CachedCameraEntity;

        std::array<char, 512> m_SpriteTextureBuffer;
        std::array<char, 256> m_SpriteMaterialBuffer;
        std::string m_SpriteTexturePath;
        bool m_OpenSpriteTextureDialog;
        Trident::ECS::Entity m_CachedSpriteEntity;

        ImGuizmo::OPERATION* m_GizmoOperation;
        ImGuizmo::MODE* m_GizmoMode;
    };
}