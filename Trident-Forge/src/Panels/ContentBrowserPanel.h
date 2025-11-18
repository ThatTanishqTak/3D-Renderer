#pragma once

#include <string>

namespace EditorPanels
{
    /**
     * @brief Lightweight placeholder for browsing project assets inside the editor.
     */
    class ContentBrowserPanel
    {
    public:
        void Update();
        void Render();

    private:
        std::string m_CurrentDirectory = "Assets";
    };
}