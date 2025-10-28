#pragma once

#include "Renderer/RenderCommand.h"

#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "ECS/Components/AnimationComponent.h"

#include "Animation/AnimationData.h"

#include <imgui.h>

#include <glm/vec2.hpp>

#include <limits>
#include <optional>
#include <string>
#include <vector>

/**
 * AnimationGraphPanel surfaces a dedicated animation authoring workspace featuring a
 * real-time viewport preview alongside a lightweight graph visualiser. The panel is
 * inspired by Unity's animator window, highlighting the current state, transition flow,
 * and parameter wiring so designers can reason about playback at a glance.
 */
class AnimationGraphPanel
{
public:
    AnimationGraphPanel() = default;

    /**
     * Assigns the registry observed by the panel. This should point at the editor registry
     * so updates immediately affect the preview scene.
     */
    void SetRegistry(Trident::ECS::Registry* registry);

    /**
     * Synchronises the selected entity with the hierarchy/inspector so the panel inspects
     * the same AnimationComponent.
     */
    void SetSelectedEntity(Trident::ECS::Entity selectedEntity);

    /**
     * Polls the current selection and refreshes layout caches so Render() can draw without
     * recomputing expensive state.
     */
    void Update();

    /**
     * Draws the ImGui widgets powering the viewport, graph, and playback tooling.
     */
    void Render();

private:
    struct GraphNode
    {
        std::string m_Label{};      ///< Display name shown inside the node.
        ImVec2 m_Position{ 0.0f, 0.0f }; ///< Top-left position in canvas space.
        ImVec2 m_Size{ 0.0f, 0.0f };     ///< Dimensions of the rendered node.
        bool m_IsActive = false;    ///< Highlights the node when its clip is currently playing.
    };

    struct GraphParameter
    {
        std::string m_Label{};      ///< Parameter name surfaced to the user.
        ImVec2 m_Position{ 0.0f, 0.0f }; ///< Top-left position in canvas space.
        ImVec2 m_Size{ 0.0f, 0.0f };     ///< Dimensions of the parameter widget.
        bool m_IsActive = false;    ///< Visual indicator showing when a parameter drives the state.
    };

    struct GraphConnection
    {
        size_t m_ParameterIndex = 0; ///< Index into m_Parameters describing the source box.
        size_t m_NodeIndex = 0;      ///< Index into m_GraphNodes describing the target node.
    };

    struct GraphTransition
    {
        size_t m_FromIndex = 0; ///< Source node index representing the transition origin.
        size_t m_ToIndex = 0;   ///< Destination node index representing the transition target.
    };

private:
    void RefreshClipLayout(const std::vector<Trident::Animation::AnimationClip>& clips);
    void EnsureParameterLayout();
    void UpdateNodeActivation();
    void UpdateParameterActivation(const Trident::AnimationComponent& component);
    void RebuildConnections();
    void DrawViewportSection();
    void DrawPlaybackControls(Trident::AnimationComponent& component, const Trident::Animation::AnimationClip* activeClip);
    void DrawParameterControls(Trident::AnimationComponent& component);
    void DrawGraphCanvas();
    void DrawGraphBackground(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size) const;
    void DrawTransitions(ImDrawList* drawList, const ImVec2& origin) const;
    void DrawParameterConnections(ImDrawList* drawList, const ImVec2& origin) const;
    void DrawNodes(ImDrawList* drawList, const ImVec2& origin) const;
    void DrawParameters(ImDrawList* drawList, const ImVec2& origin) const;

    size_t ComputeClipHash(const std::vector<Trident::Animation::AnimationClip>& clips) const;

private:
    Trident::ECS::Registry* m_Registry = nullptr; ///< Registry powering the preview scene.
    Trident::ECS::Entity m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max(); ///< Currently inspected entity.
    bool m_HasValidSelection = false; ///< Tracks whether the panel has an animation component to inspect.

    uint32_t m_ViewportID = 3U; ///< Dedicated viewport identifier reserved for the animation preview.
    glm::vec2 m_CachedViewportSize{ 0.0f }; ///< Stores the previous viewport size to detect resize events.
    ImVec2 m_ViewportBoundsMin{ 0.0f, 0.0f }; ///< Tracks the preview rectangle in screen space.
    ImVec2 m_ViewportBoundsMax{ 0.0f, 0.0f };

    std::vector<GraphNode> m_GraphNodes; ///< Cached node descriptors representing animation clips.
    std::vector<GraphParameter> m_Parameters; ///< Cached parameter descriptors representing playback controls.
    std::vector<GraphConnection> m_GraphConnections; ///< Edges linking parameter boxes to the active node.
    std::vector<GraphTransition> m_GraphTransitions; ///< Edges representing clip transitions.

    std::optional<size_t> m_ActiveNodeIndex; ///< Index of the currently active clip node when resolved.
    std::string m_ActiveClipName{}; ///< Name of the clip currently in focus, used for highlighting.
    float m_ActiveClipDuration = 0.0f; ///< Duration in seconds for the active clip to drive the timeline.

    size_t m_CachedClipHash = 0; ///< Tracks the most recent clip set so layout refreshes when assets change.
};