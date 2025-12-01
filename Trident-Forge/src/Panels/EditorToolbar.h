#pragma once

#include <functional>
#include <string>
#include <array>
#include <vulkan/vulkan.h>

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
        void SetSceneControlsEnabled(bool enabled);
        void SetPerformanceCaptureEnabled(bool enabled);

        void SetOnSceneReset(const std::function<void()>& callback);
        void SetOnPlayRequested(const std::function<void()>& callback);
        void SetOnPauseRequested(const std::function<void()>& callback);
        void SetOnStopRequested(const std::function<void()>& callback);
        void SetOnCaptureToggle(const std::function<void(bool)>& callback);
        void SetOnExportStart(const std::function<void()>& callback);
        void SetOnExportStop(const std::function<void()>& callback);
        void SetOnExportPathChanged(const std::function<void(const std::string&)>& callback);

        /**
         * @brief Mirrors the export state from the game viewport so the toolbar can surface export controls.
         */
        struct ExportUiState
        {
            bool m_HasRuntimeCamera = false;
            bool m_HasValidViewport = false;
            VkExtent2D m_RawExtent{ 0U, 0U };
            VkExtent2D m_SanitizedExtent{ 0U, 0U };
            bool m_IsRecording = false;
            float m_RecordingProgress = 0.0f;
            std::string m_OutputPath;
            std::string m_StatusMessage;
        };

        void SetExportUiState(const ExportUiState& exportUiState);
        void SetExportPath(const std::string& exportPath);

    private:
        bool RenderToolbarButton(const char* label, bool enabled);
        void RenderExportControls();

    private:
        PlaybackState m_PlaybackState = PlaybackState::Edit;
        bool m_SceneControlsEnabled = true;
        bool m_IsPerformanceCaptureEnabled = false;

        std::function<void()> m_OnSceneReset;
        std::function<void()> m_OnPlayRequested;
        std::function<void()> m_OnPauseRequested;
        std::function<void()> m_OnStopRequested;
        std::function<void(bool)> m_OnCaptureToggle;
        std::function<void()> m_OnExportStart;
        std::function<void()> m_OnExportStop;
        std::function<void(const std::string&)> m_OnExportPathChanged;

        ExportUiState m_ExportUiState{};
        std::array<char, 260> m_ExportPathBuffer{};
    };
}