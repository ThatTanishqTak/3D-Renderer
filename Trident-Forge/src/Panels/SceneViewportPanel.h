#pragma once

#include "Renderer/Renderer.h"

#include <imgui.h>

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

        [[nodiscard]] glm::vec2 GetViewportSize() const;
        [[nodiscard]] bool IsHovered() const;

    private:
        void SubmitViewportTexture(const ImVec2& viewportSize);

    private:
        ViewportInfo m_ViewportInfo{};
        bool m_IsHovered = false;
    };
}