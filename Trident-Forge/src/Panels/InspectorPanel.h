#pragma once

#include <string>

namespace EditorPanels
{
    /**
     * @brief Shows the properties for the currently selected entity.
     */
    class InspectorPanel
    {
    public:
        void Render();

        void SetSelectionLabel(const std::string& label);

    private:
        std::string m_SelectedLabel = "None";
    };
}