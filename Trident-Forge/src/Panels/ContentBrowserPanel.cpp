#include "ContentBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

ContentBrowserPanel::ContentBrowserPanel()
{
    // Resolve the assets directory relative to the application's working directory so
    // the panel always opens at the expected location, even from packaged builds.
    const std::filesystem::path l_DefaultRoot{ "Assets" };
    std::error_code l_CurrentPathError{};
    const std::filesystem::path l_WorkingDirectory = std::filesystem::current_path(l_CurrentPathError);
    if (!l_CurrentPathError)
    {
        const std::filesystem::path l_Candidate = l_WorkingDirectory / l_DefaultRoot;
        std::error_code l_ExistsError{};
        if (std::filesystem::exists(l_Candidate, l_ExistsError) && !l_ExistsError)
        {
            std::error_code l_IsDirectoryError{};
            if (std::filesystem::is_directory(l_Candidate, l_IsDirectoryError) && !l_IsDirectoryError)
            {
                m_RootDirectory = l_Candidate;
            }
        }
    }

    if (m_RootDirectory.empty())
    {
        // Fall back to the relative path so development builds can still browse assets.
        m_RootDirectory = l_DefaultRoot;
    }

    m_CurrentDirectory = m_RootDirectory;
}

void ContentBrowserPanel::Update()
{
    // Ensure the current directory still exists. If assets were deleted on disk while the editor
    // was running, this gracefully resets the browser to the root directory.
    std::error_code l_ExistsError{};
    const bool l_CurrentExists = std::filesystem::exists(m_CurrentDirectory, l_ExistsError);
    if (l_ExistsError || !l_CurrentExists)
    {
        m_CurrentDirectory = m_RootDirectory;
    }
}

void ContentBrowserPanel::Render()
{
    if (!ImGui::Begin("Content Browser"))
    {
        ImGui::End();
        return;
    }

    // Guard against missing directories so the panel communicates the issue instead of silently failing.
    std::error_code l_ExistsError{};
    const bool l_DirectoryExists = std::filesystem::exists(m_CurrentDirectory, l_ExistsError);
    if (l_ExistsError || !l_DirectoryExists)
    {
        ImGui::TextWrapped("Unable to locate assets folder at '%s'.", m_CurrentDirectory.string().c_str());
        ImGui::End();
        return;
    }

    // Provide a way to move back up the hierarchy while respecting the browser's root boundary.
    if (m_CurrentDirectory != m_RootDirectory)
    {
        if (ImGui::Button("Up"))
        {
            const std::filesystem::path l_Parent = m_CurrentDirectory.parent_path();
            std::error_code l_RelativeError{};
            std::filesystem::relative(l_Parent, m_RootDirectory, l_RelativeError);
            if (!l_RelativeError)
            {
                m_CurrentDirectory = l_Parent;
            }
            else
            {
                m_CurrentDirectory = m_RootDirectory;
            }
        }
    }
    else
    {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    }

    ImGui::SameLine();

    // Render a breadcrumb-style label so users always know where they are within Assets/.
    std::string l_DisplayPath = "Assets";
    if (m_CurrentDirectory != m_RootDirectory)
    {
        std::error_code l_RelativeError{};
        const std::filesystem::path l_Relative = std::filesystem::relative(m_CurrentDirectory, m_RootDirectory, l_RelativeError);
        if (!l_RelativeError && !l_Relative.empty())
        {
            l_DisplayPath = (std::filesystem::path("Assets") / l_Relative).string();
        }
        else
        {
            l_DisplayPath = m_CurrentDirectory.string();
        }
    }
    ImGui::TextUnformatted(l_DisplayPath.c_str());

    ImGui::Separator();

    std::vector<std::filesystem::directory_entry> l_Directories;
    std::vector<std::filesystem::directory_entry> l_Files;

    // Collect entries so folders appear before files and everything is alphabetised.
    std::error_code l_IteratorError{};
    for (std::filesystem::directory_iterator it_Entry{ m_CurrentDirectory, l_IteratorError }; !l_IteratorError && it_Entry != std::filesystem::directory_iterator{}; 
        it_Entry.increment(l_IteratorError))
    {
        if (l_IteratorError)
        {
            break;
        }

        std::error_code l_IsDirectoryError{};
        const bool l_IsDirectory = it_Entry->is_directory(l_IsDirectoryError);
        if (l_IsDirectoryError)
        {
            continue;
        }

        if (l_IsDirectory)
        {
            l_Directories.emplace_back(*it_Entry);
        }
        else
        {
            l_Files.emplace_back(*it_Entry);
        }
    }

    std::sort(l_Directories.begin(), l_Directories.end(), [](const std::filesystem::directory_entry& l_Left, const std::filesystem::directory_entry& l_Right)
        {
            return l_Left.path().filename().string() < l_Right.path().filename().string();
        });
    std::sort(l_Files.begin(), l_Files.end(), [](const std::filesystem::directory_entry& l_Left, const std::filesystem::directory_entry& l_Right)
        {
            return l_Left.path().filename().string() < l_Right.path().filename().string();
        });

    DrawEntryGrid(l_Directories, true);
    DrawEntryGrid(l_Files, false);

    ImGui::End();
}

void ContentBrowserPanel::DrawEntryGrid(const std::vector<std::filesystem::directory_entry>& entries, bool highlightAsFolder)
{
    if (entries.empty())
    {
        return;
    }

    // Calculate how many tiles fit on the current row so the layout adapts to window resizes.
    const float l_ThumbnailSize = 64.0f;
    const float l_Padding = 12.0f;
    const float l_CellSize = l_ThumbnailSize + l_Padding;
    const float l_PanelWidth = ImGui::GetContentRegionAvail().x;
    int l_ColumnCount = static_cast<int>(l_PanelWidth / l_CellSize);
    if (l_ColumnCount < 1)
    {
        l_ColumnCount = 1;
    }

    const char* l_TableIdentifier = highlightAsFolder ? "ContentBrowserGridFolders" : "ContentBrowserGridFiles";
    if (ImGui::BeginTable(l_TableIdentifier, l_ColumnCount, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoBordersInBody))
    {
        int l_ItemIndex = 0;
        for (const std::filesystem::directory_entry& it_Entry : entries)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(l_ItemIndex);

            const std::filesystem::path l_Path = it_Entry.path();
            const std::string l_Name = l_Path.filename().string();

            const ImVec2 l_ButtonSize{ l_ThumbnailSize, l_ThumbnailSize };
            const char* l_ButtonLabel = highlightAsFolder ? "Folder" : "File";
            if (ImGui::Button(l_ButtonLabel, l_ButtonSize))
            {
                // Placeholder for future interactions such as selection or drag-and-drop.
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (highlightAsFolder)
                {
                    m_CurrentDirectory = l_Path;
                }
                else
                {
                    // TODO: trigger asset preview or selection for file items.
                }
            }

            ImGui::TextWrapped("%s", l_Name.c_str());
            ImGui::PopID();
            ++l_ItemIndex;
        }

        ImGui::EndTable();
    }
}