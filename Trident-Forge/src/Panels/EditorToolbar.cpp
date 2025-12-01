#include "EditorToolbar.h"

#include "Renderer/RenderCommand.h"

#include <imgui.h>
#include <cstdio>

namespace EditorPanels
{
    void EditorToolbar::Render()
    {
        const ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
        const bool l_WindowVisible = ImGui::Begin("##EditorToolbar", nullptr, l_WindowFlags);
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

    void EditorToolbar::RenderExportControls()
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Clip Export");

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

        ImGui::Text("Capture Resolution: %ux%u", m_ExportUiState.m_SanitizedExtent.width, m_ExportUiState.m_SanitizedExtent.height);
        if (l_ShowRoundedNote)
        {
            ImGui::TextUnformatted("Note: Rounded up to even dimensions for YUV420P export.");
        }

        if (!l_HasRuntimeCamera)
        {
            ImGui::TextUnformatted("No runtime camera available for capture.");
        }
        else if (!l_HasValidViewport)
        {
            ImGui::TextUnformatted("Viewport size is invalid for capture.");
        }

        if (m_ExportUiState.m_IsRecording)
        {
            ImGui::Text("Exporting clip... %.0f%%", m_ExportUiState.m_RecordingProgress * 100.0f);
            ImGui::Text("Output: %s", m_ExportUiState.m_OutputPath.c_str());
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
            ImGui::TextUnformatted(m_ExportUiState.m_StatusMessage.c_str());
        }
    }
}