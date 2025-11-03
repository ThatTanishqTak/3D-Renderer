#pragma once

#include <imgui.h>

#include "Renderer/Renderer.h" // Need the concrete AiDebugStats definition for the cached member snapshot.

/**
 * AIDebugPanel surfaces inference statistics collected by the renderer so
 * designers can monitor AI-driven content from within the editor. The panel
 * mirrors the existing transport-style UI conventions and keeps room for
 * future analytics enhancements.
 */
class AIDebugPanel
{
public:
    AIDebugPanel();

    /**
     * Poll renderer state so cached statistics are ready for the subsequent
     * render pass. This keeps the ImGui pass free of blocking calls.
     */
    void Update();

    /**
     * Draw the AI status overview, including inference timing, queue depth,
     * blend strength controls, and a reserved preview slot.
     */
    void Render();

private:
    /**
     * Refresh member variables that depend on the latest renderer statistics.
     * Splitting this logic helps keep the render routine tidy.
     */
    void RefreshDerivedState();

private:
    Trident::Renderer::AiDebugStats m_CachedStats{}; ///< Snapshot of the renderer's AI statistics for this frame.
    bool m_ModelReady = false;                      ///< Tracks whether the AI model is currently initialised.
    bool m_TextureReady = false;                    ///< Flags when the renderer reports a valid AI preview texture.
    bool m_DataStale = false;                       ///< Signals the UI that no inference results have been produced yet.
    float m_CachedBlendStrength = 0.0f;             ///< Stores the blend factor before rendering the slider widget.
};