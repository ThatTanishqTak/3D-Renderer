#pragma once

#include "Layer/Layer.h"
#include "Events/ApplicationEvents.h"

#include "Panels/ViewportPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/GizmoState.h"

#include <string>
#include <vector>

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
    /**
     * React to engine events, including file drops routed from the operating system.
     */
    void OnEvent(Trident::Events& event) override;

private:
    bool HandleFileDrop(Trident::FileDropEvent& event);
    bool ImportDroppedAssets(const std::vector<std::string>& droppedPaths);

private:
    GizmoState m_GizmoState;
    ViewportPanel m_ViewportPanel;
    ContentBrowserPanel m_ContentBrowserPanel;
    SceneHierarchyPanel m_SceneHierarchyPanel;
    InspectorPanel m_InspectorPanel;
};