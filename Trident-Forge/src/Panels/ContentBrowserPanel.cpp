#include "ContentBrowserPanel.h"

#include <imgui.h>

#include <cstdint>
#include <vector>
#include <filesystem>

#include "Application.h"
#include "Core/Utilities.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"
#include "AI/ONNXRuntime.h"
#include "Renderer/Renderer.h"
#include "UI/FileDialog.h"

namespace UI
{
    ContentBrowserPanel::ContentBrowserPanel(): m_Engine(nullptr), m_OnnxRuntime(nullptr), m_OpenModelDialog(false), m_OpenTextureDialog(false), m_OpenSceneDialog(false), 
        m_OpenOnnxDialog(false), m_OnnxLoaded(false), m_CurrentDirectory("Assets"), m_SelectedPath("Assets"), m_ThumbnailSize(96.0f), m_ThumbnailPadding(16.0f), m_DragPayloadBuffer()
    {
        // Constructor prepares default state so the panel can lazily bind dependencies.
        m_AssetsPath = "Assets";
        m_FileIcon = Trident::Loader::TextureLoader::Load("Assets/Icons/folder.png");
    }

    ContentBrowserPanel::~ContentBrowserPanel()
    {
        // Destructor intentionally left blank; panel does not own external resources.
    }

    void ContentBrowserPanel::SetEngine(Trident::Application* engine)
    {
        // Cache the engine pointer so we can trigger scene loads from the browser.
        m_Engine = engine;
    }

    void ContentBrowserPanel::SetOnnxRuntime(Trident::AI::ONNXRuntime* onnxRuntime)
    {
        // Store the ONNX runtime so inference controls are aware of availability.
        m_OnnxRuntime = onnxRuntime;
    }

    void ContentBrowserPanel::Render()
    {
        // The content browser is split into a navigation tree and a thumbnail grid for rapid browsing.
        ImGui::Begin("Content Browser");

        if (!std::filesystem::exists(m_CurrentDirectory))
        {
            // Maintain a valid browsing context if assets are moved externally.
            m_CurrentDirectory = m_AssetsPath;
        }

        const ImVec2 l_TreePanelSize(220.0f, 0.0f);
        ImGui::BeginChild("DirectoryTree", l_TreePanelSize, true);

        ImGuiTreeNodeFlags l_BaseFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
        const bool l_IsRootSelected = m_CurrentDirectory == std::filesystem::path(m_AssetsPath);

        ImGuiTreeNodeFlags l_RootFlags = l_BaseFlags | ImGuiTreeNodeFlags_DefaultOpen;
        if (l_IsRootSelected)
        {
            l_RootFlags |= ImGuiTreeNodeFlags_Selected;
        }

        if (ImGui::TreeNodeEx("Assets", l_RootFlags))
        {
            if (ImGui::IsItemClicked())
            {
                m_CurrentDirectory = m_AssetsPath;
                m_SelectedPath = m_CurrentDirectory;
            }

            DrawDirectoryTree(m_AssetsPath);
            ImGui::TreePop();
        }

        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("DirectoryGrid", ImVec2(0.0f, 0.0f), false);

        const std::filesystem::path l_AssetsPath{ m_AssetsPath };
        if (m_CurrentDirectory != l_AssetsPath)
        {
            if (ImGui::Button("Back"))
            {
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
                m_SelectedPath = m_CurrentDirectory;
            }
        }

        ImGui::Separator();

        DrawDirectoryGrid(m_CurrentDirectory);

        ImGui::EndChild();

        ImGui::End();
    }

    void ContentBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directory)
    {
        // Traverse directories recursively so nested content remains accessible.
        if (!std::filesystem::exists(directory))
        {
            return;
        }

        for (const auto& it_Entry : std::filesystem::directory_iterator(directory))
        {
            if (!it_Entry.is_directory())
            {
                continue;
            }

            const std::filesystem::path& l_Path = it_Entry.path();
            const std::string l_Name = l_Path.filename().string();

            ImGuiTreeNodeFlags l_Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
            const bool l_IsSelected = m_CurrentDirectory == l_Path;
            if (l_IsSelected)
            {
                l_Flags |= ImGuiTreeNodeFlags_Selected;
            }

            const bool l_Opened = ImGui::TreeNodeEx(l_Name.c_str(), l_Flags);

            if (ImGui::IsItemClicked())
            {
                m_CurrentDirectory = l_Path;
                m_SelectedPath = l_Path;
            }

            if (l_Opened)
            {
                DrawDirectoryTree(l_Path);
                ImGui::TreePop();
            }
        }
    }

    void ContentBrowserPanel::DrawDirectoryGrid(const std::filesystem::path& directory)
    {
        // Display contents in a responsive thumbnail grid that emulates modern editors.
        if (!std::filesystem::exists(directory))
        {
            return;
        }

        const float l_CellSize = m_ThumbnailSize + m_ThumbnailPadding;
        const float l_PanelWidth = ImGui::GetContentRegionAvail().x;
        int l_ColumnCount = static_cast<int>(l_PanelWidth / l_CellSize);
        if (l_ColumnCount < 1)
        {
            l_ColumnCount = 1;
        }

        ImGui::Columns(l_ColumnCount, nullptr, false);

        for (const auto& it_Entry : std::filesystem::directory_iterator(directory))
        {
            const std::filesystem::path& l_Path = it_Entry.path();
            const std::string l_Filename = l_Path.filename().string();
            const bool l_IsDirectory = it_Entry.is_directory();

            ImGui::PushID(l_Filename.c_str());

            const bool l_IsSelected = m_SelectedPath == l_Path;
            const ImVec4 l_DirectoryColor = ImVec4(0.25f, 0.30f, 0.60f, 1.0f);
            const ImVec4 l_FileColor = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
            const ImVec4 l_SelectedColor = ImVec4(0.80f, 0.45f, 0.15f, 1.0f);

            const ImVec4 l_ButtonColor = l_IsSelected ? l_SelectedColor : (l_IsDirectory ? l_DirectoryColor : l_FileColor);

            ImGui::PushStyleColor(ImGuiCol_Button, l_ButtonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, l_ButtonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, l_ButtonColor);

            // Placeholder buttons act as thumbnails until GPU-backed icons are introduced.
            ImGui::Button("", ImVec2(m_ThumbnailSize, m_ThumbnailSize));

            ImGui::PopStyleColor(3);

            if (ImGui::IsItemClicked())
            {
                m_SelectedPath = l_Path;
            }

            // Promote assets to drag sources so the viewport can accept direct model imports.
            if (!l_IsDirectory)
            {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    // Cache the absolute path on the panel instance to keep the payload memory valid.
                    m_DragPayloadBuffer = l_Path.string();

                    const char* l_PayloadData = m_DragPayloadBuffer.c_str();
                    const size_t l_PayloadSize = (m_DragPayloadBuffer.size() + 1) * sizeof(char);
                    ImGui::SetDragDropPayload("TRIDENT_CONTENT_BROWSER_PATH", l_PayloadData, l_PayloadSize);

                    ImGui::TextUnformatted(l_Filename.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (l_IsDirectory)
                {
                    m_CurrentDirectory = l_Path;
                    m_SelectedPath = l_Path;
                }
                else
                {
                    m_SelectedPath = l_Path;
                    // Future improvement: trigger context-aware asset previews or imports here.
                }
            }

            ImGui::TextWrapped("%s", l_Filename.c_str());

            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);

        // Future improvement: convert loaded icons into ImGui descriptor sets for real thumbnails.
    }
}