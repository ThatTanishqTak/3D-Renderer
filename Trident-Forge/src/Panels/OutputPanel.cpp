#include "OutputPanel.h"

#include "Application.h"

#include <vector>

#include <imgui.h>

namespace UI
{
	OutputPanel::OutputPanel()
	{
		// Leave empty
	}

	OutputPanel::~OutputPanel()
	{
		// Leave empty
	}

	void OutputPanel::Render()
	{
        std::vector<Trident::Utilities::ConsoleLog::Entry> l_LogEntries = Trident::Utilities::ConsoleLog::GetSnapshot();

        ImGui::Begin("Output");

        if (ImGui::Button("Clear"))
        {
            Trident::Utilities::ConsoleLog::Clear();
            m_LastConsoleEntryCount = 0;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);

        ImGui::SameLine();
        if (ImGui::Checkbox("Enable Performance Capture", &m_EnablePerformanceCapture))
        {
            m_Renderer.SetPerformanceCaptureEnabled(m_EnablePerformanceCapture);
        }

        ImGui::Separator();

        ImGui::Checkbox("Errors", &m_ShowConsoleErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_ShowConsoleWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Logs", &m_ShowConsoleLogs);

        ImGui::Separator();

        ImGui::BeginChild("OutputLogScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

        for (const Trident::Utilities::ConsoleLog::Entry& it_Entry : l_LogEntries)
        {
            if (!ShouldDisplayConsoleEntry(it_Entry.Level))
            {
                continue;
            }

            const std::string l_Timestamp = FormatConsoleTimestamp(it_Entry.Timestamp);
            const ImVec4 l_Colour = GetConsoleColour(it_Entry.Level);

            ImGui::PushStyleColor(ImGuiCol_Text, l_Colour);
            ImGui::Text("[%s] %s", l_Timestamp.c_str(), it_Entry.Message.c_str());
            ImGui::PopStyleColor();
        }

        if (m_ConsoleAutoScroll && !l_LogEntries.empty() && l_LogEntries.size() != m_LastConsoleEntryCount)
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();

        m_LastConsoleEntryCount = l_LogEntries.size();

        ImGui::End();
	}

    // Convert a timestamp to a human readable clock string that fits in the console.
    std::string OutputPanel::FormatConsoleTimestamp(const std::chrono::system_clock::time_point& timePoint)
    {
        std::time_t l_TimeT = std::chrono::system_clock::to_time_t(timePoint);
        std::tm l_LocalTime{};

#if defined(_MSC_VER)
        localtime_s(&l_LocalTime, &l_TimeT);
#else
        localtime_r(&l_TimeT, &l_LocalTime);
#endif

        std::ostringstream l_Stream;
        l_Stream << std::put_time(&l_LocalTime, "%H:%M:%S");
        return l_Stream.str();
    }

    // Decide whether an entry should be shown given the active severity toggles.
    bool OutputPanel::ShouldDisplayConsoleEntry(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::critical:
        case spdlog::level::err:
            return m_ShowConsoleErrors;
        case spdlog::level::warn:
            return m_ShowConsoleWarnings;
        default:
            return m_ShowConsoleLogs;
        }
    }

    // Pick a colour for a log entry so important events stand out while browsing history.
    ImVec4 OutputPanel::GetConsoleColour(spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::critical:
        case spdlog::level::err:
            return { 0.94f, 0.33f, 0.33f, 1.0f };
        case spdlog::level::warn:
            return { 0.97f, 0.78f, 0.26f, 1.0f };
        case spdlog::level::debug:
        case spdlog::level::trace:
            return { 0.60f, 0.80f, 0.98f, 1.0f };
        default:
            return { 0.85f, 0.85f, 0.85f, 1.0f };
        }
    }
}