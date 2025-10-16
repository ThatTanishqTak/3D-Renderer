#pragma once

#include "Layer/Layer.h"

#include "Panels/ViewportPanel.h"
#include "Panels/ContentBrowserPanel.h"


class ApplicationLayer : public Trident::Layer
{
public:
    /**
     * Prepare application-specific resources (scenes, editor state, etc.).
     */
    void Initialize() override;
    /**
     * Release resources acquired during Initialize().
     */
    void Shutdown() override;

    /**
     * Execute per-frame simulation such as tools or gameplay logic.
     */
    void Update() override;
    /**
     * Submit draw calls and UI for the current frame.
     */
    void Render() override;

private:
    ViewportPanel m_ViewportPanel;
    ContentBrowserPanel m_ContentBrowserPanel;
};