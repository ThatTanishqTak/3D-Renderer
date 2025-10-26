#include "ConsolePanel.h"

#include <array>
#include <string>
#include <vector>

namespace
{
    // Helper array describing the severity toggles that should appear in the toolbar.
    constexpr std::array<spdlog::level::level_enum, 6> g_DisplayLevels =
    {
        spdlog::level::trace,
        spdlog::level::debug,
        spdlog::level::info,
        spdlog::level::warn,
        spdlog::level::err,
        spdlog::level::critical
    };
}

ConsolePanel::ConsolePanel()
{
    m_LevelVisibility.fill(false);
    // Default to surfacing important messages while keeping verbose trace/debug muted until explicitly enabled.
    SetLevelVisibility(spdlog::level::info, true);
    SetLevelVisibility(spdlog::level::warn, true);
    SetLevelVisibility(spdlog::level::err, true);
    SetLevelVisibility(spdlog::level::critical, true);
}

void ConsolePanel::Update()
{
    // No time-based behaviour yet; placeholder for future fade/animation logic.
}

void ConsolePanel::Render()
{
    // Dock-friendly window that mirrors the runtime log feed inside the editor UI.
    if (!ImGui::Begin("Console"))
    {
        ImGui::End();
        return;
    }

    // Toolbar: offer buttons and toggles so designers can manage the console feed.
    if (ImGui::BeginChild("ConsoleToolbar", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() * 2.2f), false, ImGuiWindowFlags_NoScrollbar))
    {
        // Clearing the buffer provides a clean slate when diagnosing new issues.
        if (ImGui::Button("Clear"))
        {
            Trident::Utilities::ConsoleLog::Clear();
            m_PreviousEntryCount = 0;
            m_ScrollToBottomRequested = true;
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Levels:");
        ImGui::SameLine();

        // Severity toggles mimic Unity's info/warning/error filters while keeping extra levels available.
        for (spdlog::level::level_enum it_Level : g_DisplayLevels)
        {
            const int l_LevelIndex = static_cast<int>(it_Level);
            if (l_LevelIndex < 0 || l_LevelIndex >= static_cast<int>(m_LevelVisibility.size()))
            {
                continue;
            }

            const char* l_Label = nullptr;
            switch (it_Level)
            {
            case spdlog::level::trace: l_Label = "Trace"; break;
            case spdlog::level::debug: l_Label = "Debug"; break;
            case spdlog::level::info: l_Label = "Info"; break;
            case spdlog::level::warn: l_Label = "Warn"; break;
            case spdlog::level::err: l_Label = "Error"; break;
            case spdlog::level::critical: l_Label = "Critical"; break;
            default: l_Label = "Level"; break;
            }

            ImGui::PushID(l_LevelIndex);
            bool l_Visible = m_LevelVisibility[static_cast<std::size_t>(l_LevelIndex)];
            if (ImGui::Checkbox(l_Label, &l_Visible))
            {
                m_LevelVisibility[static_cast<std::size_t>(l_LevelIndex)] = l_Visible;
            }
            ImGui::PopID();
            ImGui::SameLine();
        }

        // Offer a text filter so users can narrow the log to specific subsystems or keywords.
        m_TextFilter.Draw("Filter", 180.0f);
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Scrollable log region that renders the buffered console entries.
    if (ImGui::BeginChild("ConsoleScrollRegion", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const std::vector<Trident::Utilities::ConsoleLog::Entry> l_Entries = Trident::Utilities::ConsoleLog::GetSnapshot();

        // Remember if the user was reading the latest entry so new rows can gently auto-scroll.
        const bool l_WasAtBottom = ImGui::GetScrollY() >= (ImGui::GetScrollMaxY() - 1.0f);
        const bool l_HasNewEntries = l_Entries.size() > m_PreviousEntryCount;

        for (const Trident::Utilities::ConsoleLog::Entry& it_Entry : l_Entries)
        {
            if (!IsLevelVisible(it_Entry.Level))
            {
                continue;
            }

            if (!m_TextFilter.PassFilter(it_Entry.Message.c_str()))
            {
                continue;
            }

            ImVec4 l_Colour{};
            const char* l_Icon = "";
            DescribeLevel(it_Entry.Level, l_Colour, l_Icon);

            ImGui::PushStyleColor(ImGuiCol_Text, l_Colour);
            ImGui::TextWrapped("%s %s", l_Icon, it_Entry.Message.c_str());
            ImGui::PopStyleColor();
        }

        // Preserve manual scroll-back by only snapping to the bottom when the user was already there or after clear.
        if ((l_HasNewEntries && l_WasAtBottom) || m_ScrollToBottomRequested)
        {
            ImGui::SetScrollHereY(1.0f);
            m_ScrollToBottomRequested = false;
        }

        m_PreviousEntryCount = l_Entries.size();
    }
    ImGui::EndChild();

    // Future improvements: consider collapsing duplicate messages, surfacing stack traces with highlighting,
    // persisting filter selections between editor sessions, and surfacing context actions for copying entries.

    ImGui::End();
}

void ConsolePanel::SetLevelVisibility(spdlog::level::level_enum level, bool visible)
{
    const int l_LevelIndex = static_cast<int>(level);
    if (l_LevelIndex < 0 || l_LevelIndex >= static_cast<int>(m_LevelVisibility.size()))
    {
        return;
    }

    m_LevelVisibility[static_cast<std::size_t>(l_LevelIndex)] = visible;
}

bool ConsolePanel::IsLevelVisible(spdlog::level::level_enum level) const
{
    const int l_LevelIndex = static_cast<int>(level);
    if (l_LevelIndex < 0 || l_LevelIndex >= static_cast<int>(m_LevelVisibility.size()))
    {
        return false;
    }

    return m_LevelVisibility[static_cast<std::size_t>(l_LevelIndex)];
}

void ConsolePanel::DescribeLevel(spdlog::level::level_enum level, ImVec4& colourOut, const char*& iconOut) const
{
    switch (level)
    {
    case spdlog::level::trace:
        colourOut = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        iconOut = "·";
        break;
    case spdlog::level::debug:
        colourOut = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
        iconOut = "D";
        break;
    case spdlog::level::info:
        colourOut = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        iconOut = "i";
        break;
    case spdlog::level::warn:
        colourOut = ImVec4(1.0f, 0.85f, 0.45f, 1.0f);
        iconOut = "!";
        break;
    case spdlog::level::err:
        colourOut = ImVec4(1.0f, 0.5f, 0.5f, 1.0f);
        iconOut = "?";
        break;
    case spdlog::level::critical:
        colourOut = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        iconOut = "?";
        break;
    default:
        colourOut = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
        iconOut = "";
        break;
    }
}