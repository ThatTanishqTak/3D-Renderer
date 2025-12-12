#include "ContentBrowserPanel.h"

#include "Core/Utilities.h"

#include <imgui.h>

#include <algorithm>
#include <system_error>

namespace EditorPanels
{
    void ContentBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDirectory)
    {
        m_RootDirectory = rootDirectory;
        if (m_RootDirectory.empty())
        {
            m_CurrentDirectory.clear();
            m_Entries.clear();
            m_StatusMessage = "No content directory configured.";
            m_EntriesDirty = false;

            return;
        }

        std::error_code l_Ec;
        if (!std::filesystem::exists(m_RootDirectory, l_Ec) || !std::filesystem::is_directory(m_RootDirectory, l_Ec))
        {
            m_CurrentDirectory.clear();
            m_Entries.clear();
            m_StatusMessage = "Content root does not exist: " + m_RootDirectory.string();
            m_EntriesDirty = false;

            return;
        }

        m_CurrentDirectory = m_RootDirectory;
        m_StatusMessage.clear();
        m_EntriesDirty = true;
    }

    void ContentBrowserPanel::SetOnAssetActivated(const std::function<void(const std::filesystem::path&)>& callback)
    {
        m_OnAssetActivated = callback;
    }

    void ContentBrowserPanel::Update()
    {
        if (m_EntriesDirty)
        {
            RefreshEntries();
        }
    }

    void ContentBrowserPanel::RefreshEntries()
    {
        m_Entries.clear();

        if (m_CurrentDirectory.empty())
        {
            m_StatusMessage = "No content directory configured.";
            m_EntriesDirty = false;
            return;
        }

        std::error_code l_Ec;
        if (!std::filesystem::exists(m_CurrentDirectory, l_Ec) || !std::filesystem::is_directory(m_CurrentDirectory, l_Ec))
        {
            m_StatusMessage = "Directory does not exist: " + m_CurrentDirectory.string();
            m_EntriesDirty = false;

            return;
        }

        for (const auto& it_Entry : std::filesystem::directory_iterator(m_CurrentDirectory, l_Ec))
        {
            if (l_Ec)
            {
                break;
            }

            m_Entries.push_back(it_Entry);
        }

        std::sort(m_Entries.begin(), m_Entries.end(),
            [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b)
            {
                const bool l_ADir = a.is_directory();
                const bool l_BDir = b.is_directory();
                if (l_ADir != l_BDir)
                {
                    // Directories first.
                    return l_ADir && !l_BDir;
                }

                return a.path().filename().string() < b.path().filename().string();
            });

        if (m_Entries.empty())
        {
            m_StatusMessage = "Directory is empty.";
        }
        else
        {
            m_StatusMessage.clear();
        }

        m_EntriesDirty = false;
    }

    std::string ContentBrowserPanel::FormatPayloadPath(const std::filesystem::directory_entry& entry) const
    {
        // Ensure the payload string includes the root prefix so the importer can open the real file on disk instead of
        // receiving a truncated relative path.
        std::filesystem::path l_RelativePath = entry.path().lexically_relative(m_RootDirectory);

        // Fallback to the full entry path when relative conversion fails so drag payloads still reference a readable file.
        std::filesystem::path l_PayloadPath = l_RelativePath.empty() ? entry.path() : m_RootDirectory / l_RelativePath;

        std::string l_PayloadText = Trident::Utilities::FileManagement::NormalizePath(l_PayloadPath.string());

        // Append a trailing slash for directories so payloads are distinct from files and remain easy to spot in the tooltip.
        if (entry.is_directory() && !l_PayloadText.empty() && l_PayloadText.back() != '/')
        {
            l_PayloadText.push_back('/');
        }

        return l_PayloadText;
    }

    void ContentBrowserPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Content Browser");
        (void)l_WindowVisible;

        if (m_RootDirectory.empty())
        {
            ImGui::TextWrapped("No content root configured.");
            ImGui::End();

            return;
        }

        ImGui::TextWrapped("Root: %s", m_RootDirectory.string().c_str());
        ImGui::TextWrapped("Current: %s", m_CurrentDirectory.string().c_str());

        if (m_CurrentDirectory != m_RootDirectory)
        {
            if (ImGui::Button("Up"))
            {
                std::filesystem::path l_Parent = m_CurrentDirectory.parent_path();
                if (l_Parent.empty())
                {
                    // Clamp to root when hitting the filesystem root.
                    m_CurrentDirectory = m_RootDirectory;
                }
                else
                {
                    const std::string l_RootStr = m_RootDirectory.string();
                    const std::string l_ParentStr = l_Parent.string();
                    const bool l_InsideRoot = l_ParentStr.size() >= l_RootStr.size() && l_ParentStr.rfind(l_RootStr, 0) == 0;

                    m_CurrentDirectory = l_InsideRoot ? l_Parent : m_RootDirectory;
                }

                m_EntriesDirty = true;
            }
        }

        ImGui::Separator();

        if (!m_StatusMessage.empty())
        {
            ImGui::TextWrapped(m_StatusMessage.c_str());
        }

        for (const auto& it_Entry : m_Entries)
        {
            const bool l_IsDirectory = it_Entry.is_directory();
            const std::string l_Name = it_Entry.path().filename().string();
            std::string l_Label = l_IsDirectory ? l_Name + "/" : l_Name;

            const bool l_Selected = false;
            if (ImGui::Selectable(l_Label.c_str(), l_Selected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    if (l_IsDirectory)
                    {
                        m_CurrentDirectory = it_Entry.path();
                        m_EntriesDirty = true;
                    }
                    else if (m_OnAssetActivated)
                    {
                        m_OnAssetActivated(it_Entry.path());
                    }
                }
            }

            // Surface the current entry as a drag source with a payload string the viewport can consume.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const std::string l_PayloadText = FormatPayloadPath(it_Entry);

                // Provide the payload and a readable tooltip to distinguish files from directories.
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", l_PayloadText.c_str(), l_PayloadText.size());
                ImGui::TextUnformatted(l_PayloadText.c_str());

                ImGui::EndDragDropSource();
            }
        }

        ImGui::End();
    }
}