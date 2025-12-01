#include "ConsolePanel.h"

#include <imgui.h>

namespace EditorPanels
{
    void ConsolePanel::Initialize()
    {
        // Seed the visibility map so render toggles always have an entry before any filtering occurs.
        m_LevelVisibility[spdlog::level::trace] = false;
        m_LevelVisibility[spdlog::level::debug] = false;
        m_LevelVisibility[spdlog::level::info] = true;
        m_LevelVisibility[spdlog::level::warn] = true;
        m_LevelVisibility[spdlog::level::err] = true;
        m_LevelVisibility[spdlog::level::critical] = true;

        // Placeholder log to demonstrate rendering; future work can hook into the engine logger.
        m_Messages.push_back({ spdlog::level::info, "Console initialized" });
    }

    void ConsolePanel::Update()
    {
        // At present the console simply retains its existing messages. Future updates can pull from a shared sink.
    }

    void ConsolePanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Console");
        (void)l_WindowVisible;
        // Submit unconditionally so dockspace coverage includes the console window even when collapsed.

        // Level visibility toggles mirror the configuration used by the layer.
        for (auto& [l_Level, l_IsVisible] : m_LevelVisibility)
        {
            const char* l_Label = spdlog::level::to_string_view(l_Level).data();
            ImGui::Checkbox(l_Label, &l_IsVisible);
        }

        ImGui::Separator();

        for (const ConsoleMessage& l_Message : m_Messages)
        {
            const auto l_It = m_LevelVisibility.find(l_Message.m_Level);
            const bool l_IsVisible = l_It == m_LevelVisibility.end() ? true : l_It->second;
            if (!l_IsVisible)
            {
                continue;
            }

            ImGui::TextWrapped(l_Message.m_Text.c_str());
        }

        ImGui::End();
    }

    void ConsolePanel::SetLevelVisibility(spdlog::level::level_enum level, bool isVisible)
    {
        // Store the preference so both rendering and future ingestion paths honor the requested filter.
        m_LevelVisibility[level] = isVisible;
    }
}