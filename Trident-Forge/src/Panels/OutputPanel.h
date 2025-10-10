#pragma once

#include <string>

#include <imgui.h>
#include <spdlog/spdlog.h>

namespace UI
{
	class OutputPanel
	{
	public:
		OutputPanel();
		~OutputPanel();

		void Render();

	private:
		std::string FormatConsoleTimestamp(const std::chrono::system_clock::time_point& timePoint);
		bool ShouldDisplayConsoleEntry(spdlog::level::level_enum level);
		ImVec4 GetConsoleColour(spdlog::level::level_enum level);

	private:
		// Console window configuration toggles stored across frames for a consistent user experience.
		bool m_ShowConsoleErrors = true;
		bool m_ShowConsoleWarnings = true;
		bool m_ShowConsoleLogs = true;
		bool m_ConsoleAutoScroll = true;
		size_t m_LastConsoleEntryCount = 0;
	};
}