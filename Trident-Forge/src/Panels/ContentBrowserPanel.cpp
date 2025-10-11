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
    ContentBrowserPanel::ContentBrowserPanel(): m_Engine(nullptr), m_OnnxRuntime(nullptr), m_ModelPath(), m_TexturePath(), m_ScenePath(), m_OnnxPath(), m_OpenModelDialog(false),
        m_OpenTextureDialog(false), m_OpenSceneDialog(false), m_OpenOnnxDialog(false), m_OnnxLoaded(false), m_CurrentDirectory("Assets"), m_SelectedPath("Assets"),
        m_ThumbnailSize(96.0f), m_ThumbnailPadding(16.0f)
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

    void ContentBrowserPanel::DrawModelSection()
    {
        // Provide an entry point for artists to load geometry assets.
        ImGui::Text("Model: %s", m_ModelPath.c_str());
        if (ImGui::Button("Load Model"))
        {
            m_OpenModelDialog = true;
            ImGui::OpenPopup("ModelDialog");
        }

        if (!m_OpenModelDialog)
        {
            return;
        }

        if (Trident::UI::FileDialog::Open("ModelDialog", m_ModelPath, ".fbx"))
        {
            auto a_ModelData = Trident::Loader::ModelLoader::Load(m_ModelPath);
            Trident::Application::GetRenderer().UploadMesh(a_ModelData.Meshes, a_ModelData.Materials);
            m_OpenModelDialog = false;
        }

        if (!ImGui::IsPopupOpen("ModelDialog"))
        {
            m_OpenModelDialog = false;
        }
    }

    void ContentBrowserPanel::DrawTextureSection()
    {
        // Allow users to stage textures for material authoring.
        ImGui::Text("Texture: %s", m_TexturePath.c_str());
        if (ImGui::Button("Load Texture"))
        {
            m_OpenTextureDialog = true;
            ImGui::OpenPopup("TextureDialog");
        }

        if (!m_OpenTextureDialog)
        {
            return;
        }

        if (Trident::UI::FileDialog::Open("TextureDialog", m_TexturePath))
        {
            auto a_Texture = Trident::Loader::TextureLoader::Load(m_TexturePath);
            Trident::Application::GetRenderer().UploadTexture(a_Texture);
            m_OpenTextureDialog = false;
        }

        if (!ImGui::IsPopupOpen("TextureDialog"))
        {
            m_OpenTextureDialog = false;
        }
    }

    void ContentBrowserPanel::DrawSceneSection()
    {
        // Gate scene loading through the engine pointer so we avoid dereferencing null.
        const bool l_HasEngine = m_Engine != nullptr;

        ImGui::BeginDisabled(!l_HasEngine);
        ImGui::Text("Scene: %s", m_ScenePath.c_str());
        if (ImGui::Button("Load Scene"))
        {
            m_OpenSceneDialog = true;
            ImGui::OpenPopup("SceneDialog");
        }
        ImGui::EndDisabled();

        if (m_OpenSceneDialog)
        {
            if (Trident::UI::FileDialog::Open("SceneDialog", m_ScenePath, ".scene"))
            {
                if (m_Engine != nullptr)
                {
                    m_Engine->LoadScene(m_ScenePath);
                }
                m_OpenSceneDialog = false;
            }

            if (!ImGui::IsPopupOpen("SceneDialog"))
            {
                m_OpenSceneDialog = false;
            }
        }

        if (!l_HasEngine)
        {
            ImGui::TextUnformatted("Scene loading is disabled until the engine is initialised.");
        }
    }

    void ContentBrowserPanel::DrawOnnxSection()
    {
        // Present AI tooling while ensuring controls remain safe when runtime support is absent.
        const bool l_HasRuntime = m_OnnxRuntime != nullptr;

        ImGui::BeginDisabled(!l_HasRuntime);
        ImGui::Text("ONNX: %s", m_OnnxPath.c_str());
        if (ImGui::Button("Load ONNX"))
        {
            m_OpenOnnxDialog = true;
            ImGui::OpenPopup("ONNXDialog");
        }
        ImGui::EndDisabled();

        if (m_OpenOnnxDialog)
        {
            if (Trident::UI::FileDialog::Open("ONNXDialog", m_OnnxPath, ".onnx"))
            {
                if (m_OnnxRuntime != nullptr)
                {
                    m_OnnxLoaded = m_OnnxRuntime->LoadModel(m_OnnxPath);
                }
                m_OpenOnnxDialog = false;
            }

            if (!ImGui::IsPopupOpen("ONNXDialog"))
            {
                m_OpenOnnxDialog = false;
            }
        }
        else if (!l_HasRuntime)
        {
            ImGui::TextUnformatted("ONNX runtime is unavailable; plug-in support will enable this later.");
        }

        if (!l_HasRuntime)
        {
            return;
        }

        if (m_OnnxLoaded)
        {
            if (ImGui::Button("Run Inference"))
            {
                std::vector<float> l_Input{ 0.0f };
                std::vector<int64_t> l_Shape{ 1 };
                auto a_Output = m_OnnxRuntime->Run(l_Input, l_Shape);
                if (!a_Output.empty())
                {
                    TR_INFO("Inference result: {}", a_Output[0]);
                }
            }
        }
        else
        {
            ImGui::TextUnformatted("Load an ONNX model to enable inference testing.");
        }
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