#include "EditorToolbar.h"

#include "Renderer/RenderCommand.h"

#include <imgui.h>
#include <cstdio>
#include <algorithm>

namespace EditorPanels
{
    void EditorToolbar::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("EditorToolbar", nullptr, 0);
        (void)l_WindowVisible;
        // Always push toolbar contents so dockspace validation sees this fixed-position tool strip even when hidden.

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

        const bool l_SceneClicked = RenderToolbarButton("Scene", true);
        if (l_SceneClicked && m_OnSceneReset)
        {
            m_OnSceneReset();
        }

        ImGui::SameLine();

        const bool l_PlayClicked = RenderToolbarButton("Play", m_SceneControlsEnabled && m_PlaybackState != PlaybackState::Playing);
        if (l_PlayClicked && m_OnPlayRequested)
        {
            m_OnPlayRequested();
        }

        ImGui::SameLine();

        const bool l_PauseClicked = RenderToolbarButton("Pause", m_SceneControlsEnabled && m_PlaybackState == PlaybackState::Playing);
        if (l_PauseClicked && m_OnPauseRequested)
        {
            m_OnPauseRequested();
        }

        ImGui::SameLine();

        const bool l_StopClicked = RenderToolbarButton("Stop", m_SceneControlsEnabled && m_PlaybackState != PlaybackState::Edit);
        if (l_StopClicked && m_OnStopRequested)
        {
            m_OnStopRequested();
        }

        ImGui::SameLine();

        const bool l_PerfClicked = RenderToolbarButton("Performance Capture", true);
        if (l_PerfClicked)
        {
            const bool l_NewCaptureState = !m_IsPerformanceCaptureEnabled;
            m_IsPerformanceCaptureEnabled = l_NewCaptureState;

            // Update the renderer capture mode to keep swapchain command buffer lifetime correct per the LunarG guides.
            Trident::RenderCommand::SetPerformanceCaptureEnabled(l_NewCaptureState);

            if (m_OnCaptureToggle)
            {
                m_OnCaptureToggle(l_NewCaptureState);
            }
        }

        ImGui::PopStyleVar();

        RenderDatasetCaptureControls();
        RenderExportControls();

        ImGui::End();
    }

    void EditorToolbar::SetPlaybackState(PlaybackState playbackState)
    {
        m_PlaybackState = playbackState;
    }

    void EditorToolbar::SetSceneControlsEnabled(bool enabled)
    {
        m_SceneControlsEnabled = enabled;
    }

    void EditorToolbar::SetPerformanceCaptureEnabled(bool enabled)
    {
        m_IsPerformanceCaptureEnabled = enabled;
    }

    void EditorToolbar::SetDatasetCaptureEnabled(bool enabled)
    {
        // Keep a local copy of the capture flag so the UI reflects renderer state across frames.
        m_IsDatasetCaptureEnabled = enabled;
    }

    void EditorToolbar::SetDatasetCaptureDirectory(const std::string& captureDirectory)
    {
        // Update the editable buffer so users can see the active directory even when it was configured elsewhere.
        std::snprintf(m_DatasetCaptureDirectoryBuffer.data(), m_DatasetCaptureDirectoryBuffer.size(), "%s", captureDirectory.c_str());
        UpdateDatasetDirectoryBuffer();
    }

    void EditorToolbar::SetDatasetCaptureInterval(uint32_t sampleInterval)
    {
        // Clamp to a valid range because the renderer enforces a minimum sample interval of 1 frame.
        m_DatasetCaptureInterval = std::max<uint32_t>(1U, sampleInterval);
    }

    void EditorToolbar::SetOnSceneReset(const std::function<void()>& callback)
    {
        m_OnSceneReset = callback;
    }

    void EditorToolbar::SetOnPlayRequested(const std::function<void()>& callback)
    {
        m_OnPlayRequested = callback;
    }

    void EditorToolbar::SetOnPauseRequested(const std::function<void()>& callback)
    {
        m_OnPauseRequested = callback;
    }

    void EditorToolbar::SetOnStopRequested(const std::function<void()>& callback)
    {
        m_OnStopRequested = callback;
    }

    void EditorToolbar::SetOnCaptureToggle(const std::function<void(bool)>& callback)
    {
        m_OnCaptureToggle = callback;
    }

    void EditorToolbar::SetOnDatasetCaptureToggle(const std::function<void(bool)>& callback)
    {
        m_OnDatasetCaptureToggle = callback;
    }

    void EditorToolbar::SetOnDatasetCaptureDirectoryChanged(const std::function<void(const std::string&)>& callback)
    {
        m_OnDatasetCaptureDirectoryChanged = callback;
    }

    void EditorToolbar::SetOnDatasetSampleIntervalChanged(const std::function<void(uint32_t)>& callback)
    {
        m_OnDatasetSampleIntervalChanged = callback;
    }

    void EditorToolbar::SetOnExportStart(const std::function<void()>& callback)
    {
        m_OnExportStart = callback;
    }

    void EditorToolbar::SetOnExportStop(const std::function<void()>& callback)
    {
        m_OnExportStop = callback;
    }

    void EditorToolbar::SetOnExportPathChanged(const std::function<void(const std::string&)>& callback)
    {
        m_OnExportPathChanged = callback;
    }

    void EditorToolbar::SetExportUiState(const ExportUiState& exportUiState)
    {
        m_ExportUiState = exportUiState;
    }

    void EditorToolbar::SetExportPath(const std::string& exportPath)
    {
        // Keep the buffer synchronized with the latest path reported by the runtime viewport panel.
        std::snprintf(m_ExportPathBuffer.data(), m_ExportPathBuffer.size(), "%s", exportPath.c_str());
        m_ExportUiState.m_OutputPath = exportPath;
    }

    bool EditorToolbar::RenderToolbarButton(const char* label, bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        const bool l_Clicked = ImGui::Button(label);
        ImGui::EndDisabled();

        return l_Clicked;
    }

    void EditorToolbar::RenderDatasetCaptureControls()
    {
        ImGui::Separator();
        ImGui::TextWrapped("Dataset Capture");
        ImGui::TextWrapped("Enable dataset capture from the toolbar to avoid a separate floating window.");

        bool l_CaptureEnabled = m_IsDatasetCaptureEnabled;
        if (ImGui::Checkbox("Enable dataset capture", &l_CaptureEnabled))
        {
            m_IsDatasetCaptureEnabled = l_CaptureEnabled;

            if (m_OnDatasetCaptureToggle)
            {
                m_OnDatasetCaptureToggle(l_CaptureEnabled);
            }
        }

        // Allow authors to change the dataset directory without reopening a dedicated window.
        if (ImGui::InputText("Capture directory", m_DatasetCaptureDirectoryBuffer.data(), m_DatasetCaptureDirectoryBuffer.size()))
        {
            UpdateDatasetDirectoryBuffer();

            if (m_OnDatasetCaptureDirectoryChanged)
            {
                m_OnDatasetCaptureDirectoryChanged(std::string(m_DatasetCaptureDirectoryBuffer.data()));
            }
        }

        int l_SampleInterval = static_cast<int>(m_DatasetCaptureInterval);
        if (ImGui::InputInt("Sample interval", &l_SampleInterval))
        {
            l_SampleInterval = std::max(1, l_SampleInterval);
            m_DatasetCaptureInterval = static_cast<uint32_t>(l_SampleInterval);

            if (m_OnDatasetSampleIntervalChanged)
            {
                m_OnDatasetSampleIntervalChanged(m_DatasetCaptureInterval);
            }
        }

        ImGui::TextWrapped("Sampling every %u frame(s)", m_DatasetCaptureInterval);
    }

    void EditorToolbar::RenderExportControls()
    {
        ImGui::Separator();
        ImGui::TextWrapped("Clip Export");

        // Surface the current export path for editing so the toolbar owns the export UI.
        if (ImGui::InputText("Export Path", m_ExportPathBuffer.data(), m_ExportPathBuffer.size()))
        {
            if (m_OnExportPathChanged)
            {
                m_OnExportPathChanged(std::string(m_ExportPathBuffer.data()));
            }
        }

        const bool l_HasRuntimeCamera = m_ExportUiState.m_HasRuntimeCamera;
        const bool l_HasValidViewport = m_ExportUiState.m_HasValidViewport;
        const bool l_ShowRoundedNote = m_ExportUiState.m_RawExtent.width != m_ExportUiState.m_SanitizedExtent.width ||
            m_ExportUiState.m_RawExtent.height != m_ExportUiState.m_SanitizedExtent.height;

        ImGui::TextWrapped("Capture Resolution: %ux%u", m_ExportUiState.m_SanitizedExtent.width, m_ExportUiState.m_SanitizedExtent.height);
        if (l_ShowRoundedNote)
        {
            ImGui::TextWrapped("Note: Rounded up to even dimensions for YUV420P export.");
        }

        if (!l_HasRuntimeCamera)
        {
            ImGui::TextWrapped("No runtime camera available for capture.");
        }
        else if (!l_HasValidViewport)
        {
            ImGui::TextWrapped("Viewport size is invalid for capture.");
        }

        if (m_ExportUiState.m_IsRecording)
        {
            ImGui::TextWrapped("Exporting clip... %.0f%%", m_ExportUiState.m_RecordingProgress * 100.0f);
            ImGui::TextWrapped("Output: %s", m_ExportUiState.m_OutputPath.c_str());
            if (RenderToolbarButton("Stop Export", true) && m_OnExportStop)
            {
                m_OnExportStop();
            }
        }
        else
        {
            const bool l_CanStartExport = l_HasRuntimeCamera && l_HasValidViewport;
            if (RenderToolbarButton("Start Clip Export", l_CanStartExport) && m_OnExportStart)
            {
                m_OnExportStart();
            }
        }

        if (!m_ExportUiState.m_StatusMessage.empty())
        {
            ImGui::TextWrapped(m_ExportUiState.m_StatusMessage.c_str());
        }
    }

    void EditorToolbar::UpdateDatasetDirectoryBuffer()
    {
        // Keep the directory buffer null terminated so ImGui input cannot overrun the storage.
        if (m_DatasetCaptureDirectoryBuffer.empty())
        {
            return;
        }

        m_DatasetCaptureDirectoryBuffer.back() = '\0';
    }
}