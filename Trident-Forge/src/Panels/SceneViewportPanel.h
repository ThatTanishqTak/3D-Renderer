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
     * @brief Displays the editor camera output inside an ImGui viewport.
     *
     * The viewport forwards its size to the renderer each frame so the Vulkan swapchain
     * can resize offscreen targets without violating image ownership rules described in
     * https://vulkan.lunarg.com/doc/sdk/latest/windows/renderpass.html.
     */
    class SceneViewportPanel
    {
    public:
        SceneViewportPanel();

        void Render();
        void Update();

        [[nodiscard]] glm::vec2 GetViewportSize() const;
        [[nodiscard]] bool IsHovered() const;
        [[nodiscard]] bool IsFocused() const;
        [[nodiscard]] bool ContainsPoint(const ImVec2& screenPoint) const;
        [[nodiscard]] Trident::ECS::Entity GetSelectedEntity() const;

        void SetGizmoState(Trident::GizmoState* gizmoState);
        void SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped);
        void SetSelectedEntity(Trident::ECS::Entity entity);
        void SetRegistry(Trident::ECS::Registry* registry);

    private:
        void SubmitViewportTexture(const ImVec2& viewportSize);

    private:
        Trident::ViewportInfo m_ViewportInfo{}; ///< Mirrors the renderer viewport configuration for the scene view.
        bool m_IsHovered = false; ///< Tracks whether the viewport window is hovered for tool input routing.
        bool m_IsFocused = false; ///< Tracks whether the viewport window has keyboard focus for shortcuts.
        ImVec2 m_BoundsMin{ 0.0f, 0.0f }; ///< Cached minimum bound used for hit-testing OS drag-and-drop coordinates.
        ImVec2 m_BoundsMax{ 0.0f, 0.0f }; ///< Cached maximum bound used for hit-testing OS drag-and-drop coordinates.
        Trident::GizmoState* m_GizmoState = nullptr; ///< Shared gizmo state pointer so inspector/viewport remain synchronized.
        Trident::ECS::Registry* m_Registry = nullptr; ///< Active registry supplied by the scene bridge for entity queries.
        Trident::ECS::Entity m_SelectedEntity = 0; ///< Currently selected entity identifier mirrored from the hierarchy.
        std::function<void(const std::vector<std::string>&)> m_OnAssetsDropped; ///< Callback invoked when assets are dropped.
    };
}