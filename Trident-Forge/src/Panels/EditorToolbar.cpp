#include "EditorToolbar.h"

#include "Renderer/RenderCommand.h"

#include <imgui.h>

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

        const bool l_PlayClicked = RenderToolbarButton("Play", m_PlaybackState != PlaybackState::Playing);
        if (l_PlayClicked && m_OnPlayRequested)
        {
            m_OnPlayRequested();
        }

        ImGui::SameLine();

        const bool l_PauseClicked = RenderToolbarButton("Pause", m_PlaybackState == PlaybackState::Playing);
        if (l_PauseClicked && m_OnPauseRequested)
        {
            m_OnPauseRequested();
        }

        ImGui::SameLine();

        const bool l_StopClicked = RenderToolbarButton("Stop", m_PlaybackState != PlaybackState::Edit);
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

        ImGui::End();
    }

    void EditorToolbar::SetPlaybackState(PlaybackState playbackState)
    {
        m_PlaybackState = playbackState;
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

    bool EditorToolbar::RenderToolbarButton(const char* label, bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        const bool l_Clicked = ImGui::Button(label);
        ImGui::EndDisabled();

        return l_Clicked;
    }
}