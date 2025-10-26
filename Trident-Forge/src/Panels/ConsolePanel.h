#pragma once

#include "Core/Utilities.h"

#include <array>
#include <cstddef>

#include <imgui.h>

/**
 * ConsolePanel mirrors the editor log output inside an ImGui docked window.
 * It exposes severity toggles, a text filter, and log rendering helpers so
 * designers can triage diagnostics without leaving the editor.
 */
class ConsolePanel
{
public:
    ConsolePanel();

    // Advance any time-based state (reserved for future features such as fade-outs).
    void Update();
    // Draw the console window, including filter widgets and the scrollable log history.
    void Render();

    // Allow the application layer to adjust visibility per severity when seeding defaults.
    void SetLevelVisibility(spdlog::level::level_enum level, bool visible);

private:
    // Return whether a severity toggle currently permits the given level.
    bool IsLevelVisible(spdlog::level::level_enum level) const;
    // Resolve a colour/icon pair for the supplied severity so each row stands out.
    void DescribeLevel(spdlog::level::level_enum level, ImVec4& colourOut, const char*& iconOut) const;

private:
    // Persistent filter toggles per severity level (trace/debug/info/warn/error/critical/off).
    std::array<bool, spdlog::level::n_levels> m_LevelVisibility{};
    // Text search filter that supports substring matches against message contents.
    ImGuiTextFilter m_TextFilter;
    // Cache the entry count so we can auto-scroll only when new rows arrive while at the bottom.
    std::size_t m_PreviousEntryCount = 0;
    // Track whether the next render should snap to the bottom, e.g., after pressing Clear().
    bool m_ScrollToBottomRequested = true;
};