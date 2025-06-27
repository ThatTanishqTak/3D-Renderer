#include "UI/FileDialog.h"

#include <imgui.h>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

namespace Trident
{
    namespace UI
    {
        namespace
        {
            fs::path s_CurrentDirectory = fs::current_path();
        }

        bool FileDialog::Open(const char* id, std::string& path, const char* extension)
        {
            bool chosen = false;
            bool open = true;
            if (ImGui::BeginPopupModal(id, &open))
            {
                ImGui::TextUnformatted(s_CurrentDirectory.string().c_str());
                if (ImGui::Button(".."))
                {
                    if (s_CurrentDirectory.has_parent_path())
                        s_CurrentDirectory = s_CurrentDirectory.parent_path();
                }

                ImGui::BeginChild("##browser", ImVec2(500, 300), true);

                std::vector<fs::directory_entry> entries;
                for (auto& entry : fs::directory_iterator(s_CurrentDirectory))
                    entries.push_back(entry);

                std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b)
                    {
                        if (a.is_directory() != b.is_directory())
                            return a.is_directory() > b.is_directory();
                        return a.path().filename().string() < b.path().filename().string();
                    });

                for (const auto& entry : entries)
                {
                    std::string name = entry.path().filename().string();
                    if (entry.is_directory())
                    {
                        if (ImGui::Selectable((name + "/").c_str(), false))
                        {
                            s_CurrentDirectory /= name;
                        }
                    }
                    else
                    {
                        bool matches = true;
                        if (extension && *extension)
                            matches = entry.path().extension() == extension;
                        if (matches && ImGui::Selectable(name.c_str(), false))
                        {
                            path = entry.path().string();
                            chosen = true;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }

                ImGui::EndChild();

                if (ImGui::Button("Cancel"))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }

            return chosen;
        }
    }
}