#pragma once

#include "Renderer/Renderer.h"
#include "GizmoState.h"
#include "ECS/Registry.h"

#include <imgui.h>
#include <functional>
#include <vector>

namespace EditorPanels
{
    /**
     * @brief Displays the runtime camera output while respecting renderer viewport routing.
     */
    class GameViewportPanel
    {
    public:
        GameViewportPanel();

        void Render();
        void Update();

        [[nodiscard]] bool IsHovered() const;
        [[nodiscard]] bool IsFocused() const;

        void SetGizmoState(Trident::GizmoState* gizmoState);
        void SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped);
        void SetRegistry(Trident::ECS::Registry* registry);

    private:
        void SubmitViewportTexture(const ImVec2& viewportSize);

    private:
        Trident::ViewportInfo m_ViewportInfo{}; ///< Renderer viewport metadata for the runtime view.
        bool m_IsHovered = false; ///< Hover tracking used to gate runtime-focused shortcuts.
        bool m_IsFocused = false; ///< Focus tracking used to route keyboard input to the runtime viewport.
        Trident::GizmoState* m_GizmoState = nullptr; ///< Shared gizmo state pointer for future runtime gizmo support.
        Trident::ECS::Registry* m_Registry = nullptr; ///< Registry pointer retained for future runtime entity queries.
        std::function<void(const std::vector<std::string>&)> m_OnAssetsDropped; ///< Callback invoked when assets are dropped.
    };
}