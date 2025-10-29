#pragma once

#include <imgui.h>

#include <filesystem>

namespace Trident
{
    namespace UI
    {
        // Centralises ImGui styling so editor themes can be swapped without touching the layer wiring.
        class ImGuiStyleManager
        {
        public:
            enum class Profile
            {
                ProfessionalDark
            };

            ImGuiStyleManager();

            // Applies the active style profile to the supplied ImGui IO configuration and theme tables.
            void ApplyStyle(ImGuiIO& io);

        private:
            // Applies the professional dark theme profile used by the editor today.
            void ApplyProfessionalDark(ImGuiIO& io, ImGuiStyle& style);

            // TODO: Support loading style definitions from external assets so commercial presets can be dropped in without recompiling.
            void LoadProfileFromAssets(const std::filesystem::path& profilePath);

            Profile m_CurrentProfile;
        };
    }
}