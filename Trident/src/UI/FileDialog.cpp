#include "UI/FileDialog.h"

#include "Application.h"
#include "Loader/TextureLoader.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <string>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace Trident
{
    namespace UI
    {
        namespace
        {
            fs::path s_CurrentDirectory = fs::current_path();

            class IconLibrary
            {
            public:
                struct Icon
                {
                    ImTextureID m_TextureId = 0; ///< Descriptor consumed by ImGui widgets.
                    ImVec2 m_Size{ 20.0f, 20.0f };     ///< Preferred display size used as a fallback when no texture is available.

                    bool IsValid() const { return m_TextureId != 0; }
                };

                IconLibrary() = default;

                Icon GetIconForEntry(const fs::directory_entry& entry)
                {
                    EnsureLoaded();

                    if (entry.is_directory())
                    {
                        const bool l_IsEmpty = IsDirectoryEmpty(entry.path());

                        return l_IsEmpty ? m_EmptyFolderIcon : m_FilledFolderIcon;
                    }

                    std::string l_Extension = entry.path().extension().string();
                    ToLowerInPlace(l_Extension);

                    if (m_ImageExtensions.count(l_Extension) > 0)
                    {
                        return m_ImageFileIcon;
                    }

                    if (m_ModelExtensions.count(l_Extension) > 0)
                    {
                        return m_ModelFileIcon;
                    }

                    return m_GenericFileIcon;
                }

            private:
                void EnsureLoaded()
                {
                    if (m_IconsLoaded)
                    {
                        return;
                    }

                    LoadIcons();
                    m_IconsLoaded = true;
                }

                void LoadIcons()
                {
                    // Icon placeholders – provide the final asset locations when the artwork is available.
                    const std::string l_EmptyFolderIconPath = "Assets/Icons/folder.png";
                    const std::string l_FilledFolderIconPath = "Assets/Icons/folder.png";
                    const std::string l_ImageFileIconPath{}; // TODO: Set to an image file icon path, e.g. "Assets/Icons/file_image.png".
                    const std::string l_ModelFileIconPath{}; // TODO: Set to a model file icon path, e.g. "Assets/Icons/file_model.png".
                    const std::string l_GenericFileIconPath{}; // TODO: Set to a generic file icon path, e.g. "Assets/Icons/file_generic.png".

                    m_EmptyFolderIcon = LoadIcon(l_EmptyFolderIconPath);
                    m_FilledFolderIcon = LoadIcon(l_FilledFolderIconPath);
                    m_ImageFileIcon = LoadIcon(l_ImageFileIconPath);
                    m_ModelFileIcon = LoadIcon(l_ModelFileIconPath);
                    m_GenericFileIcon = LoadIcon(l_GenericFileIconPath);
                }

                Icon LoadIcon(const std::string& path) const
                {
                    Icon l_Icon{};

                    if (path.empty())
                    {
                        return l_Icon;
                    }

                    std::error_code l_ExistsError{};
                    if (!fs::exists(path, l_ExistsError) || l_ExistsError)
                    {
                        // Quietly ignore missing icons so the dialog remains functional while assets are staged.
                        return l_Icon;
                    }

                    Loader::TextureData l_TextureData = Loader::TextureLoader::Load(path);
                    if (l_TextureData.Pixels.empty())
                    {
                        return l_Icon;
                    }

                    Renderer::ImGuiTexture* a_Texture = Application::GetRenderer().CreateImGuiTexture(l_TextureData);
                    if (a_Texture != nullptr)
                    {
                        l_Icon.m_TextureId = a_Texture->m_Descriptor;
                        l_Icon.m_Size = ImVec2(static_cast<float>(a_Texture->m_Extent.width), static_cast<float>(a_Texture->m_Extent.height));
                    }

                    return l_Icon;
                }

                static void ToLowerInPlace(std::string& value)
                {
                    for (char& l_Character : value)
                    {
                        l_Character = static_cast<char>(std::tolower(static_cast<unsigned char>(l_Character)));
                    }
                }

                static bool IsDirectoryEmpty(const fs::path& directory)
                {
                    std::error_code l_Error{};
                    fs::directory_iterator l_Begin(directory, l_Error);
                    if (l_Error)
                    {
                        // Treat I/O failures as empty to avoid crashing the dialog when permissions are restricted.
                        return true;
                    }

                    return l_Begin == fs::directory_iterator{};
                }

                bool m_IconsLoaded = false;
                Icon m_EmptyFolderIcon{};
                Icon m_FilledFolderIcon{};
                Icon m_ImageFileIcon{};
                Icon m_ModelFileIcon{};
                Icon m_GenericFileIcon{};

                const std::unordered_set<std::string> m_ImageExtensions
                {
                    ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tiff", ".tif", ".hdr", ".psd", ".gif", ".exr", ".dds"
                };

                const std::unordered_set<std::string> m_ModelExtensions
                {
                    ".fbx", ".obj", ".gltf", ".glb", ".dae", ".stl", ".ply", ".3ds", ".blend", ".x", ".lwo", ".abc"
                };
            };

            IconLibrary& GetIconLibrary()
            {
                static IconLibrary s_IconLibrary{};

                return s_IconLibrary;
            }
        }

        bool FileDialog::Open(const char* id, std::string& path, const char* extension)
        {
            bool l_FileChosen = false;
            bool l_Open = true;
            if (ImGui::BeginPopupModal(id, &l_Open))
            {
                IconLibrary& l_IconLibrary = GetIconLibrary();

                ImGui::TextUnformatted(s_CurrentDirectory.string().c_str());
                if (ImGui::Button(".."))
                {
                    if (s_CurrentDirectory.has_parent_path())
                    {
                        s_CurrentDirectory = s_CurrentDirectory.parent_path();
                    }
                }

                ImGui::BeginChild("##browser", ImVec2(500, 300), true);

                std::vector<fs::directory_entry> l_Entries;
                std::error_code l_IteratorError{};
                for (fs::directory_iterator it_Entry{ s_CurrentDirectory, l_IteratorError }; it_Entry != fs::directory_iterator{}; ++it_Entry)
                {
                    l_Entries.push_back(*it_Entry);
                }

                std::sort(l_Entries.begin(), l_Entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b)
                    {
                        if (a.is_directory() != b.is_directory())
                        {
                            return a.is_directory() > b.is_directory();
                        }
                        
                        return a.path().filename().string() < b.path().filename().string();
                    });

                if (ImGui::BeginTable("##FileBrowserTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
                {
                    ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);

                    for (const auto& it_Entry : l_Entries)
                    {
                        const fs::directory_entry& l_Entry = it_Entry;
                        const std::string l_Name = l_Entry.path().filename().string();
                        const bool l_IsDirectory = l_Entry.is_directory();

                        ImGui::PushID(l_Name.c_str());

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);

                        const IconLibrary::Icon l_Icon = l_IconLibrary.GetIconForEntry(l_Entry);
                        const ImVec2 l_IconSize = l_Icon.IsValid() ? l_Icon.m_Size : ImVec2(18.0f, 18.0f);

                        if (l_Icon.IsValid())
                        {
                            ImGui::Image(l_Icon.m_TextureId, l_IconSize);
                        }
                        else
                        {
                            ImGui::Dummy(l_IconSize);
                        }
                        ImGui::TableSetColumnIndex(1);

                        const std::string l_DisplayName = l_IsDirectory ? l_Name + "/" : l_Name;
                        const bool l_Selected = ImGui::Selectable(l_DisplayName.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                        if (l_Selected)
                        {
                            if (l_IsDirectory)
                            {
                                s_CurrentDirectory /= l_Name;
                            }
                            else
                            {
                                bool l_Matches = true;
                                if (extension != nullptr && *extension != '\0')
                                {
                                    l_Matches = l_Entry.path().extension() == extension;
                                }

                                if (l_Matches)
                                {
                                    path = l_Entry.path().string();
                                    l_FileChosen = true;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }

                ImGui::EndChild();

                if (ImGui::Button("Cancel"))
                {
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            return l_FileChosen;
        }
    }
}