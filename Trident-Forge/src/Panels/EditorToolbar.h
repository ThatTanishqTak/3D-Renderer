#pragma once

#include <functional>
#include <string>

namespace EditorPanels
{
    /**
     * @brief Renders the top toolbar used to drive scene playback.
     *
     * The toolbar exposes a small set of callbacks so the owning EditorLayer can
     * connect to the actual scene management logic without hard dependencies.
     * Buttons are intentionally mirrored after the ImGui docking guide to ensure
     * consistent authoring workflows while keeping Vulkan command buffers intact.
     */
    class EditorToolbar
    {
    public:
        enum class PlaybackState
        {
            Edit,
            Playing,
            Paused
        };

        void Render();

        void SetPlaybackState(PlaybackState playbackState);
        void SetPerformanceCaptureEnabled(bool enabled);

        void SetOnSceneReset(const std::function<void()>& callback);
        void SetOnPlayRequested(const std::function<void()>& callback);
        void SetOnPauseRequested(const std::function<void()>& callback);
        void SetOnStopRequested(const std::function<void()>& callback);
        void SetOnCaptureToggle(const std::function<void(bool)>& callback);

    private:
        bool RenderToolbarButton(const char* label, bool enabled);

    private:
        PlaybackState m_PlaybackState = PlaybackState::Edit;
        bool m_IsPerformanceCaptureEnabled = false;

        std::function<void()> m_OnSceneReset;
        std::function<void()> m_OnPlayRequested;
        std::function<void()> m_OnPauseRequested;
        std::function<void()> m_OnStopRequested;
        std::function<void(bool)> m_OnCaptureToggle;
    };
}