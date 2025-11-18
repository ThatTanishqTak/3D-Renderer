#pragma once

#include <string>

namespace EditorPanels
{
    /**
     * @brief Lists entities in the active scene.
     */
    class SceneHierarchyPanel
    {
    public:
        void Render();

    private:
        std::string m_StatusMessage = "No entities registered";
    };
}