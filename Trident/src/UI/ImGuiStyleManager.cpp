#include "UI/ImGuiStyleManager.h"

namespace Trident
{
    namespace UI
    {
        ImGuiStyleManager::ImGuiStyleManager() : m_CurrentProfile(Profile::ProfessionalDark)
        {

        }

        void ImGuiStyleManager::ApplyStyle(ImGuiIO& io)
        {
            // Ensure the editor supports modern docking workflows.
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

            // Allow ImGui to create multi-viewport windows so tool panels can detach.
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

            // Start from ImGui's stock dark palette so custom tweaks have a predictable base.
            ImGui::StyleColorsDark();

            ImGuiStyle& l_Style = ImGui::GetStyle();

            switch (m_CurrentProfile)
            {
            case Profile::ProfessionalDark:
            default:
                ApplyProfessionalDark(io, l_Style);
                break;
            }
        }

        void ImGuiStyleManager::ApplyProfessionalDark(ImGuiIO& io, ImGuiStyle& style)
        {
            // When viewports are active we flatten the rounding and alpha so external windows match the main viewport.
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                style.WindowRounding = 0.0f;
                style.Colors[ImGuiCol_WindowBg].w = 1.0f;
            }

            // TODO: Expand the professional dark theme with additional colour tweaks that match other DCC tools.
        }

        void ImGuiStyleManager::LoadProfileFromAssets(const std::filesystem::path& profilePath)
        {
            // TODO: Implement asset-driven theme loading to rapidly prototype new editor looks.
            (void)profilePath;
        }
    }
}