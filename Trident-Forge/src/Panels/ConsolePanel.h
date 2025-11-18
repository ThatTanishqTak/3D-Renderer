#pragma once

#include <spdlog/spdlog.h>
#include <deque>
#include <string>
#include <unordered_map>

namespace EditorPanels
{
    /**
     * @brief Minimal console panel that surfaces logged messages with level filtering.
     *
     * The implementation is intentionally lightweight so future work can integrate the
     * engine's central logging sink while keeping the editor UI stable.
     */
    class ConsolePanel
    {
    public:
        void Initialize();
        void Update();
        void Render();

        void SetLevelVisibility(spdlog::level::level_enum level, bool isVisible);

    private:
        struct ConsoleMessage
        {
            spdlog::level::level_enum m_Level;
            std::string m_Text;
        };

        std::unordered_map<spdlog::level::level_enum, bool> m_LevelVisibility{};
        std::deque<ConsoleMessage> m_Messages;
    };
}