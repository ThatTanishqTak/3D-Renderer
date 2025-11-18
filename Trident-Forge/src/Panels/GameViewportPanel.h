#pragma once

#include "Renderer/Renderer.h"

#include <imgui.h>

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

    private:
        void SubmitViewportTexture(const ImVec2& viewportSize);

    private:
        ViewportInfo m_ViewportInfo{};
    };
}