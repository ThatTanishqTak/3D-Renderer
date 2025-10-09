#pragma once

#include <array>
#include <string>

#include "ECS/Registry.h"

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
        void SetSelectedEntity(Trident::ECS::Entity a_SelectedEntity);

        /**
         * @brief Provide access to the gizmo state so the inspector can drive ImGuizmo controls.
         */
        void SetGizmoState(ImGuizmo::OPERATION* a_Operation, ImGuizmo::MODE* a_Mode);

        /**
         * @brief Draw all component editors and helper dialogs.
         */
        void Render();

    private:
        void DrawTransformSection(Trident::ECS::Registry& a_Registry);
        void DrawCameraSection(Trident::ECS::Registry& a_Registry);
        void DrawSpriteSection(Trident::ECS::Registry& a_Registry);
        void DrawLightSection(Trident::ECS::Registry& a_Registry);
        void DrawGizmoControls();
        void DrawEditorCameraSection();
        void DrawMaterialsSection();
        void DrawLiveReloadSection();

        void ResetCameraCacheIfNeeded(Trident::ECS::Entity a_SelectedEntity, Trident::ECS::Registry& a_Registry);
        void ResetSpriteCacheIfNeeded(Trident::ECS::Entity a_SelectedEntity, Trident::ECS::Registry& a_Registry);

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